#!/usr/bin/env python3
"""Safely unpack one Windows commander-cat export for the local GUI."""

from __future__ import annotations

import argparse
import csv
import io
import hashlib
import json
import shutil
import sys
import zipfile
from pathlib import Path, PurePosixPath


ALLOWED = {
    "commander_cat_inventory.csv",
    "commander_cat_manifest.json",
}
FORMAT_VERSION = 2
RULE_VERSION = "embryo-2026-10-06-v2"
MAX_FILE_SIZE = 2 * 1024 * 1024
MAX_TOTAL_SIZE = 4 * 1024 * 1024


def fail(message: str) -> None:
    print(json.dumps({"ok": False, "error": message}, ensure_ascii=False))
    raise SystemExit(1)


def safe_zip_entries(archive: Path) -> dict[str, bytes]:
    try:
        handle = zipfile.ZipFile(archive)
    except (OSError, zipfile.BadZipFile) as error:
        fail(f"ZIP 无法读取：{error}")

    result: dict[str, bytes] = {}
    total = 0
    with handle:
        for info in handle.infolist():
            path = PurePosixPath(info.filename.replace("\\", "/"))
            if info.is_dir():
                fail("ZIP 中不应包含目录")
            if path.is_absolute() or ".." in path.parts or len(path.parts) != 1:
                fail(f"ZIP 包含不安全路径：{info.filename}")
            name = path.name
            if name not in ALLOWED:
                fail(f"ZIP 包含不支持的文件：{name}")
            if name in result:
                fail(f"ZIP 文件重复：{name}")
            if info.flag_bits & 0x1:
                fail("不支持加密 ZIP")
            if info.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
                fail(f"ZIP 使用了不支持的压缩方式：{name}")
            if info.file_size > MAX_FILE_SIZE:
                fail(f"文件过大：{name}")
            total += info.file_size
            if total > MAX_TOTAL_SIZE:
                fail("导入文件总大小超过限制")
            result[name] = handle.read(info)
    if not result:
        fail("ZIP 中没有可导入的 CSV")
    return result


def direct_csv(path: Path) -> tuple[str, bytes]:
    name = path.name
    if name not in ALLOWED:
        fail(f"导入文件必须是：{' 或 '.join(sorted(ALLOWED))}；旧设置文件不再导入")
    try:
        size = path.stat().st_size
        if size > MAX_FILE_SIZE:
            fail(f"文件过大：{name}")
        return name, path.read_bytes()
    except OSError as error:
        fail(f"无法读取 {name}：{error}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("inputs", nargs="+")
    args = parser.parse_args()

    if len(args.inputs) > 2:
        fail("一次最多选择两个文件")
    paths = [Path(value) for value in args.inputs]
    if len(paths) > 1 and any(path.suffix.lower() == ".zip" for path in paths):
        fail("ZIP 必须单独选择；两个文件时请选择库存 CSV 和版本清单 JSON")

    files: dict[str, bytes] = {}
    if len(paths) == 1 and paths[0].suffix.lower() == ".zip":
        files = safe_zip_entries(paths[0])
    else:
        for path in paths:
            name, content = direct_csv(path)
            if name in files:
                fail(f"重复选择了 {name}")
            files[name] = content

    # Validate the entire bundle before touching even the staging output. The
    # GUI validates species, talent structure and instance IDs in a second pass.
    manifest_name = "commander_cat_manifest.json"
    inventory_name = "commander_cat_inventory.csv"
    if set(files) != ALLOWED:
        fail("缺少新版库存或版本清单。旧版 data 不兼容，请在 Windows 更新后重新导出。")
    try:
        manifest = json.loads(files[manifest_name].decode("utf-8-sig"))
    except (ValueError, UnicodeError):
        fail("版本清单不是合法 UTF-8 JSON")
    if not isinstance(manifest, dict):
        fail("版本清单必须是对象")
    if type(manifest.get("format_version")) is not int or manifest["format_version"] != FORMAT_VERSION:
        fail(f"库存格式版本不兼容，需要 {FORMAT_VERSION}；原库存未改动")
    if manifest.get("rule_version") != RULE_VERSION:
        fail(f"评分规则版本不兼容，需要 {RULE_VERSION}；请对齐 Windows 后重新导出")
    if manifest.get("inventory_sha256") != hashlib.sha256(files[inventory_name]).hexdigest():
        fail("库存文件校验失败，版本清单与 CSV 不匹配")
    try:
        rows = list(csv.reader(io.StringIO(files[inventory_name].decode("utf-8-sig")), strict=True))
    except (UnicodeError, csv.Error):
        fail("库存不是合法 UTF-8 CSV")
    if not rows or rows[0] != ["instance_id", "cat_id", "nickname", "initial_talent_ids"]:
        fail("库存表头不是新版四字段格式；不能给旧文件补版本号后导入")
    if any(len(row) != 4 for row in rows[1:]):
        fail("库存存在列数不正确的记录")
    output = Path(args.output)
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    for name, content in files.items():
        (output / name).write_bytes(content)

    print(json.dumps({"ok": True, "files": sorted(files)}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except SystemExit:
        raise
    except Exception as error:  # Keep the GUI error useful without a traceback.
        print(json.dumps({"ok": False, "error": str(error)}, ensure_ascii=False))
        raise SystemExit(1)
