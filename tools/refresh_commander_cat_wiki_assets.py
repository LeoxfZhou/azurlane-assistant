#!/usr/bin/env python3
"""Download the 34 commander-cat avatars from the BWIKI MediaWiki API."""

from __future__ import annotations

import csv
import argparse
import hashlib
import json
import pathlib
import shutil
import sys
import tempfile
import time
import urllib.parse
import urllib.request
from urllib.error import HTTPError, URLError


ROOT = pathlib.Path(__file__).resolve().parents[1]
DATA = ROOT / "data" / "commander_cats"
ASSETS = ROOT / "data" / "assets" / "commander_cats"
STAGING = ROOT / "cache" / "commander_cats.pending"
PENDING_MANIFEST = ROOT / "cache" / "commander_cat_image_sources.csv.pending"
PREVIEW = ROOT / "cache" / "commander_cat_wiki_refresh_preview.json"
API = "https://wiki.biligame.com/blhx/api.php"
PAGE = "指挥喵"


def fetch(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": "AzurLaneCommanderCatLocal/1.0"})
    for attempt in range(4):
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                return response.read()
        except (HTTPError, URLError):
            if attempt == 3:
                raise
            time.sleep(2 ** attempt)
    raise RuntimeError("unreachable")


def api(params: dict[str, str]) -> dict:
    url = API + "?" + urllib.parse.urlencode(params)
    return json.loads(fetch(url))


def write_json(path: pathlib.Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")
    temporary.replace(path)


def existing_hashes() -> dict[str, str]:
    manifest = DATA / "commander_cat_image_sources.csv"
    if not manifest.exists():
        return {}
    with manifest.open(encoding="utf-8", newline="") as source:
        return {row["cat_id"]: row["sha256"] for row in csv.DictReader(source)}


def prepare() -> dict:
    if STAGING.exists():
        shutil.rmtree(STAGING)
    STAGING.mkdir(parents=True)
    with (DATA / "commander_cats.csv").open(encoding="utf-8", newline="") as source:
        cats = list(csv.DictReader(source))

    parsed = api({"action": "parse", "page": PAGE, "prop": "images", "format": "json"})
    available = set(parsed["parse"]["images"])
    filenames = [f"{cat['name']}头像.png" for cat in cats]
    missing = [filename for filename in filenames if filename not in available]
    if missing:
        raise RuntimeError("WIKI page does not list avatars: " + ", ".join(missing))
    info = api({
        "action": "query",
        "titles": "|".join(f"File:{filename}" for filename in filenames),
        "prop": "imageinfo",
        "iiprop": "url|size|mime",
        "format": "json",
    })
    image_by_name = {
        page["title"].split(":", 1)[-1]: page["imageinfo"][0]
        for page in info["query"]["pages"].values()
        if "imageinfo" in page
    }
    rows: list[dict[str, str]] = []
    for cat in cats:
        filename = f"{cat['name']}头像.png"
        if filename not in image_by_name:
            raise RuntimeError(f"WIKI API did not return image info: {filename}")
        image = image_by_name[filename]
        content = fetch(image["url"])
        digest = hashlib.sha256(content).hexdigest()
        target = STAGING / f"{cat['cat_id']}.png"
        with tempfile.NamedTemporaryFile(dir=STAGING, delete=False) as temporary:
            temporary.write(content)
            temporary_path = pathlib.Path(temporary.name)
        temporary_path.replace(target)
        rows.append({
            "cat_id": cat["cat_id"],
            "wiki_file_name": filename,
            "source_page": f"https://wiki.biligame.com/blhx/{urllib.parse.quote(PAGE)}",
            "image_url": image["url"],
            "sha256": digest,
        })

    with PENDING_MANIFEST.open("w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    previous = existing_hashes()
    names = {cat["cat_id"]: cat["name"] for cat in cats}
    changes = []
    for row in rows:
        old = previous.get(row["cat_id"])
        if old != row["sha256"]:
            changes.append({
                "cat_id": row["cat_id"],
                "name": names[row["cat_id"]],
                "status": "新增" if old is None else "头像已变化",
            })
    for cat_id in sorted(set(previous) - {row["cat_id"] for row in rows}):
        changes.append({"cat_id": cat_id, "name": cat_id, "status": "WIKI清单已移除"})
    result = {
        "ok": True,
        "downloaded": len(rows),
        "changed": changes,
        "unchanged": len(rows) - sum(1 for item in changes if item["status"] != "WIKI清单已移除"),
        "applied": False,
    }
    write_json(PREVIEW, result)
    return result


def apply() -> dict:
    if not STAGING.exists() or len(list(STAGING.glob("*.png"))) != 34:
        raise RuntimeError("没有完整的34张待应用头像，请先预览刷新")
    with PENDING_MANIFEST.open(encoding="utf-8", newline="") as source:
        if sum(1 for _ in csv.DictReader(source)) != 34:
            raise RuntimeError("待应用图片清单不完整")
    placeholder = ASSETS / "placeholder.svg"
    if placeholder.exists():
        shutil.copy2(placeholder, STAGING / placeholder.name)
    backup = ASSETS.with_name(ASSETS.name + ".backup")
    if backup.exists():
        shutil.rmtree(backup)
    ASSETS.replace(backup)
    try:
        STAGING.replace(ASSETS)
        PENDING_MANIFEST.replace(DATA / "commander_cat_image_sources.csv")
    except Exception:
        if ASSETS.exists():
            ASSETS.replace(STAGING)
        backup.replace(ASSETS)
        raise
    shutil.rmtree(backup)
    result = json.loads(PREVIEW.read_text(encoding="utf-8"))
    result["applied"] = True
    write_json(PREVIEW, result)
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--prepare", action="store_true", help="下载到暂存区并生成差异预览")
    parser.add_argument("--apply", action="store_true", help="原子应用最近一次完整预览")
    args = parser.parse_args()
    if args.prepare == args.apply:
        parser.error("必须且只能指定 --prepare 或 --apply")
    try:
        result = prepare() if args.prepare else apply()
    except Exception as error:
        if args.prepare:
            if STAGING.exists():
                shutil.rmtree(STAGING)
            PENDING_MANIFEST.unlink(missing_ok=True)
        result = {"ok": False, "error": str(error), "applied": False}
        write_json(PREVIEW, result)
        print(json.dumps(result, ensure_ascii=False))
        sys.exit(1)
    print(json.dumps(result, ensure_ascii=False))


if __name__ == "__main__":
    main()
