#!/usr/bin/env python3
"""Build and refresh the offline reference data used by the unified game GUI.

The normal GUI never contacts the network.  ``prepare`` downloads a complete
candidate into the user-data staging directory; ``apply`` atomically replaces
the corresponding read-only reference files after the user confirms the diff.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import shutil
import sys
import tempfile
import urllib.parse
import urllib.request
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SHIP_DIR = ROOT / "data" / "ship_stories"
SHIP_ASSETS = ROOT / "data" / "assets" / "ships"
WIKI_API = "https://wiki.biligame.com/blhx/api.php"
WIKI_PAGE = "个人剧情任务"
USER_AGENT = "LocalGameAssistant/1.0 (personal offline cache)"


def fetch(url: str) -> bytes:
    last_error = None
    for attempt in range(4):
        request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
        try:
            with urllib.request.urlopen(request, timeout=45) as response:
                return response.read()
        except Exception as error:
            last_error = error
            if attempt != 3:
                time.sleep(1.5 * (attempt + 1))
    raise last_error


def fetch_json(url: str):
    return json.loads(fetch(url).decode("utf-8"))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_csv(path: Path, header: list[str], rows: list[list[object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w", encoding="utf-8", newline="") as output:
        writer = csv.writer(output)
        writer.writerow(header)
        writer.writerows(rows)
    os.replace(temporary, path)


def field(wikitext: str, name: str) -> str:
    match = re.search(r"^\|" + re.escape(name) + r"[ \t]*=[ \t]*([^\r\n]*)\r?$", wikitext, re.M)
    return match.group(1).strip() if match else ""


def clean_wiki(text: str) -> str:
    text = re.sub(r"<!--.*?-->", "", text, flags=re.S)
    text = re.sub(r"\{\{[^{}]*\}\}", "", text)
    text = re.sub(r"\[\[(?:[^]|]*\|)?([^]]+)\]\]", r"\1", text)
    text = re.sub(r"<[^>]+>", "", text)
    return text.replace("'''", "").strip()


def wiki_query(params: dict[str, str]):
    params = {"format": "json", "formatversion": "2", **params}
    return fetch_json(WIKI_API + "?" + urllib.parse.urlencode(params))


def parse_story_source(source: str):
    tabber = source.split("<tabber>", 1)[1].split("</tabber>", 1)[0]
    sections = re.split(r"\n\|-\|\n", tabber)
    result = []
    for section in sections:
        wiki_name, table = section.split("=", 1)
        wiki_name = wiki_name.strip()
        link = re.search(r"\[\[碧蓝回忆录/[^]|]+\|([^]]+)\]\]", table)
        story_title = clean_wiki(link.group(1)) if link else "个人剧情"
        steps = []
        for line in table.splitlines():
            if not line.startswith("|") or line.startswith("|-") or "||" not in line:
                continue
            cells = [clean_wiki(cell) for cell in line[1:].split("||")]
            if len(cells) >= 3:
                steps.append(cells[:3])
        if not steps:
            raise RuntimeError(f"{wiki_name} has no story steps")
        result.append((wiki_name, story_title, steps))
    if len(result) != 123:
        raise RuntimeError(f"personal-story page should contain 123 ships, got {len(result)}")
    return result


def load_local_ship_overrides() -> dict[str, dict[str, str]]:
    result: dict[str, dict[str, str]] = {}
    seed = ROOT / "data" / "migration" / "legacy_ship_inventory.csv"
    if not seed.exists():
        return result
    with seed.open(encoding="utf-8-sig", newline="") as source:
        for row in csv.DictReader(source):
            entry = {"display": row["name"], "alias": row["alternate_name"],
                     "camp": row["camp"], "type": row["type"]}
            result[row["name"]] = entry
            if row["alternate_name"]:
                result[row["alternate_name"]] = entry
    return result


def batch(values: list[str], size: int = 35):
    for index in range(0, len(values), size):
        yield values[index:index + size]


def build_ships(destination: Path, assets: Path) -> dict:
    parsed = wiki_query({"action": "parse", "page": WIKI_PAGE,
                         "prop": "wikitext|revid"})["parse"]
    stories = parse_story_source(parsed["wikitext"])
    names = [item[0] for item in stories]
    metadata: dict[str, str] = {}
    image_urls: dict[str, str] = {}
    for group in batch(names):
        result = wiki_query({"action": "query", "prop": "revisions",
                             "rvprop": "content", "rvslots": "main",
                             "redirects": "1", "titles": "|".join(group)})
        for page in result["query"]["pages"]:
            content = page.get("revisions", [{}])[0].get("slots", {}).get("main", {}).get("content", "")
            metadata[page["title"]] = content
    image_titles = ["文件:" + name + "头像.jpg" for name in names]
    # Biligame's proxy rejects long imageinfo query strings with status 567.
    # Small batches are slower but much more reliable and are only used during
    # explicit refresh operations.
    for group in batch(image_titles, 10):
        result = wiki_query({"action": "query", "prop": "imageinfo",
                             "iiprop": "url", "titles": "|".join(group)})
        for page in result["query"]["pages"]:
            if page.get("imageinfo"):
                image_urls[page["title"].removeprefix("文件:").removesuffix("头像.jpg")] = page["imageinfo"][0]["url"]
    overrides = load_local_ship_overrides()
    destination.mkdir(parents=True, exist_ok=True)
    assets.mkdir(parents=True, exist_ok=True)
    catalog_rows: list[list[object]] = []
    step_rows: list[list[object]] = []
    source_rows: list[list[object]] = []
    downloaded = 0
    for index, (wiki_name, story_title, steps) in enumerate(stories, 1):
        text = metadata.get(wiki_name, "")
        canonical = clean_wiki(field(text, "名称")) or wiki_name
        harmony = clean_wiki(field(text, "和谐名"))
        if "|" in harmony or "=" in harmony:
            raise RuntimeError(f"invalid harmony name parsed for {wiki_name}: {harmony}")
        override = overrides.get(wiki_name) or overrides.get(canonical) or overrides.get(harmony)
        aliases = []
        for value in (wiki_name, override["display"] if override else "",
                      override["alias"] if override else ""):
            if value and value not in (canonical, harmony) and value not in aliases:
                aliases.append(value)
        camp = override["camp"] if override else clean_wiki(field(text, "阵营"))
        ship_type = override["type"] if override else clean_wiki(field(text, "类型"))
        ship_id = f"story_{index:03d}"
        image_file = ship_id + ".jpg"
        url = image_urls.get(wiki_name, "")
        image_hash = ""
        if url:
            try:
                (assets / image_file).write_bytes(fetch(url))
                image_hash = sha256(assets / image_file)
                downloaded += 1
            except Exception:
                pass
        catalog_rows.append([ship_id, wiki_name, canonical, harmony, "|".join(aliases),
                             camp, ship_type, story_title, image_file])
        source_rows.append([ship_id, wiki_name, url, image_hash, parsed.get("revid", "")])
        for order, (chapter, requirement, reward) in enumerate(steps, 1):
            step_rows.append([ship_id, order, chapter, requirement, reward])
    write_csv(destination / "ship_stories.csv", [
        "ship_id", "wiki_name", "canonical_name", "harmony_name", "aliases", "camp",
        "ship_type", "story_title", "image_file"], catalog_rows)
    write_csv(destination / "ship_story_steps.csv", [
        "ship_id", "chapter_order", "chapter_title", "requirement", "reward"], step_rows)
    write_csv(destination / "ship_story_image_sources.csv", [
        "ship_id", "wiki_name", "source_url", "sha256", "source_revision"], source_rows)
    if downloaded != len(catalog_rows):
        raise RuntimeError(f"only downloaded {downloaded}/{len(catalog_rows)} ship portraits; old data was not changed")
    return {"records": len(catalog_rows), "steps": len(step_rows), "images": downloaded,
            "revision": parsed.get("revid", "")}


def directory_manifest(path: Path) -> dict[str, str]:
    if not path.exists():
        return {}
    return {str(item.relative_to(path)): sha256(item)
            for item in path.rglob("*") if item.is_file()}


def compare(old: Path, new: Path) -> dict:
    before, after = directory_manifest(old), directory_manifest(new)
    return {
        "added": sorted(set(after) - set(before)),
        "modified": sorted(key for key in set(before) & set(after) if before[key] != after[key]),
        "removed": sorted(set(before) - set(after)),
    }


def compare_records(old_file: Path, new_file: Path, key: str, label: str,
                    required: tuple[str, ...] = ()) -> dict:
    def load(path: Path):
        if not path.exists():
            return {}
        with path.open(encoding="utf-8-sig", newline="") as source:
            return {row[key]: row for row in csv.DictReader(source)}
    before, after = load(old_file), load(new_file)
    return {
        "added": [after[value].get(label, value) for value in sorted(set(after) - set(before))],
        "modified": [after[value].get(label, value) for value in sorted(set(after) & set(before))
                     if after[value] != before[value]],
        "removed": [before[value].get(label, value) for value in sorted(set(before) - set(after))],
        "unmatched": [row.get(label, value) for value, row in after.items()
                      if any(not row.get(field, "").strip() for field in required)],
    }


def replace_directory(source: Path, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    backup = target.with_name(target.name + ".previous")
    if backup.exists():
        shutil.rmtree(backup)
    if target.exists():
        os.replace(target, backup)
    try:
        os.replace(source, target)
    except Exception:
        if backup.exists() and not target.exists():
            os.replace(backup, target)
        raise
    if backup.exists():
        shutil.rmtree(backup)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("bootstrap", "prepare", "apply"))
    parser.add_argument("module", choices=("ships", "fleets", "all"))
    parser.add_argument("--cache-dir", type=Path)
    args = parser.parse_args()
    cache_dir = args.cache_dir or (ROOT / "cache")
    staging = cache_dir / "game_data_refresh"
    modules = ("ships",) if args.module == "all" else (args.module,)
    results = {}
    if args.module == "fleets":
        from fleet_catalog import build
        from fleet_plans import read_catalog
        current = ROOT / "data/fleet_ships"
        candidate = staging / "fleets" / "data"
        if args.action == "bootstrap":
            results['fleets'] = build(current)
        elif args.action == "prepare":
            if candidate.exists(): shutil.rmtree(candidate)
            info = build(candidate)
            before = {s['id']:s for s in read_catalog(current)['ships']}
            after = {s['id']:s for s in read_catalog(candidate)['ships']}
            info['records'] = len(after)
            info['recordDiff'] = dict(
                added=[after[k]['displayName'] for k in sorted(after.keys()-before.keys())],
                removed=[before[k]['displayName'] for k in sorted(before.keys()-after.keys())],
                modified=[after[k]['displayName'] for k in sorted(before.keys()&after.keys()) if before[k]!=after[k]],
                unmatched=[s['displayName'] for s in after.values() if not s['groups']])
            info['assetDiff'] = compare(current/'portraits', candidate/'portraits')
            results['fleets'] = info
        else:
            catalog = read_catalog(candidate)
            for portrait in catalog['portraits']:
                if portrait['status']=='cached' and sha256(candidate/'portraits'/(portrait['id']+'.jpg')) != portrait['sha256']:
                    raise ValueError('暂存头像校验失败，未应用')
            replace_directory(candidate,current)
            results['fleets'] = {'applied':True}
        print(json.dumps({'ok':True,'modules':results},ensure_ascii=False))
        return
    if args.action in ("bootstrap", "prepare"):
        for module in modules:
            if args.action == "bootstrap":
                data_target = SHIP_DIR
                asset_target = SHIP_ASSETS
            else:
                module_stage = staging / module
                if module_stage.exists():
                    shutil.rmtree(module_stage)
                data_target = module_stage / "data"
                asset_target = module_stage / "assets"
            info = build_ships(data_target, asset_target)
            if args.action == "prepare":
                old_data = SHIP_DIR
                old_assets = SHIP_ASSETS
                info["dataDiff"] = compare(old_data, data_target)
                info["assetDiff"] = compare(old_assets, asset_target)
                info["recordDiff"] = compare_records(
                    old_data / "ship_stories.csv", data_target / "ship_stories.csv",
                    "wiki_name", "canonical_name",
                    ("canonical_name", "camp", "ship_type", "story_title"))
            results[module] = info
    else:
        for module in modules:
            module_stage = staging / module
            if not (module_stage / "data").exists():
                raise RuntimeError(f"no prepared {module} update")
            replace_directory(module_stage / "data", SHIP_DIR)
            replace_directory(module_stage / "assets", SHIP_ASSETS)
            results[module] = {"applied": True}
    print(json.dumps({"ok": True, "modules": results}, ensure_ascii=False))


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(json.dumps({"ok": False, "error": str(error)}, ensure_ascii=False))
        sys.exit(1)
