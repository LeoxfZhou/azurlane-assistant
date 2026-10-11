#!/usr/bin/env python3
"""Offline warehouse inventory. Reference ratings never come from an imported save."""
import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile

FORMAT = 'azur-lane-equipment-inventory'
GRADES = ['S', 'A', 'B', 'C', 'D', '待评级']


def digest(value):
    return hashlib.sha256(json.dumps(value, ensure_ascii=False, sort_keys=True,
                                    separators=(',', ':')).encode()).hexdigest()


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(dir=path.parent, prefix=path.name + '.')
    try:
        with os.fdopen(fd, 'w', encoding='utf-8') as stream:
            json.dump(value, stream, ensure_ascii=False, indent=2)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def clean(value, label, maximum=180, empty=False):
    if not isinstance(value, str) or len(value) > maximum or any(ord(c) < 32 for c in value):
        raise ValueError(label + '格式无效')
    if not empty and not value.strip():
        raise ValueError(label + '不能为空')
    return value.strip()


def integer(value, label, lower, upper):
    if type(value) is not int or not lower <= value <= upper:
        raise ValueError(label + '必须是范围内的整数')
    return value


def empty_inventory():
    return dict(format=FORMAT, schemaVersion=1, scope='warehouse', entries=[])


def validate(document):
    if not isinstance(document, dict) or document.get('format') != FORMAT:
        raise ValueError('不是装备库存文件；请使用本模块导出的 JSON 格式')
    if type(document.get('schemaVersion')) is not int or document['schemaVersion'] != 1:
        raise ValueError('不支持此库存版本')
    if document.get('scope') != 'warehouse':
        raise ValueError('仅支持仓库库存，不接收舰船身上已装备的库存')
    entries = document.get('entries')
    if not isinstance(entries, list) or len(entries) > 20000:
        raise ValueError('库存条目过多或格式无效')
    output, seen = [], set()
    for row in entries:
        if not isinstance(row, dict):
            raise ValueError('库存条目必须是对象')
        eid = clean(row.get('equipmentId'), '装备 ID')
        name = clean(row.get('name', eid), '装备名称')
        level = row.get('enhancement')
        if level is not None:
            integer(level, '强化等级', 0, 13)
        locked = row.get('locked', False)
        if type(locked) is not bool:
            raise ValueError('锁定状态必须是布尔值')
        key = (eid, level, locked)
        if key in seen:
            raise ValueError('同装备、强化等级和锁定状态的条目重复，请先合并数量')
        seen.add(key)
        output.append(dict(equipmentId=eid, name=name,
                           quantity=integer(row.get('quantity'), '数量', 1, 999999),
                           enhancement=level, locked=locked,
                           note=clean(row.get('note', ''), '备注', 500, True)))
    output.sort(key=lambda r: (r['equipmentId'], -1 if r['enhancement'] is None else r['enhancement'], r['locked']))
    return dict(format=FORMAT, schemaVersion=1, scope='warehouse', entries=output)


def load_ratings(catalog):
    """Latest explicit assessment wins for each exact variant; absence never demotes."""
    sources = {s['id']: s for s in catalog['sources']}
    history = {}
    for row in catalog.get('assessments', []):
        if row['grade'] not in GRADES or row['sourceId'] not in sources:
            raise ValueError('评级依据不完整')
        history.setdefault(row['equipmentId'], []).append(row)
    for rows in history.values():
        rows.sort(key=lambda r: (sources[r['sourceId']]['date'], r.get('revision', 0)))
    return history


def evaluated(catalog):
    history = load_ratings(catalog)
    lookup = {e['id']: e for e in catalog['equipment']}
    output = []
    for gear in catalog['equipment']:
        rows = history.get(gear['id'], [])
        latest = rows[-1] if rows else {}
        grade = latest.get('grade', '待评级')
        # Follow all reachable research products, not only the first recipe step.
        targets, pending = [], list(gear.get('researchTo', []))
        seen = {gear['id']}
        while pending:
            eid = pending.pop(0)
            if eid in seen:
                continue
            seen.add(eid)
            target = lookup.get(eid)
            if not target:
                continue
            target_rows = history.get(eid, [])
            target_grade = target_rows[-1]['grade'] if target_rows else '待评级'
            targets.append(dict(id=eid, name=target['name'], grade=target_grade))
            pending.extend(target.get('researchTo', []))
        useful = [t for t in targets if t['grade'] in ('S', 'A', 'B')]
        # Material value is explicit and separate from combat grade. It must not
        # turn the material itself into an S-ranked combat item.
        output.append(dict(gear, grade=grade, assessment=latest or None,
                           history=rows, researchProducts=targets,
                           usefulMaterial=bool(useful)))
    return output


class Store:
    def __init__(self, user_file, catalog_path):
        self.path = Path(user_file)
        self.catalog = json.loads(Path(catalog_path).read_text(encoding='utf-8'))
        self.items = evaluated(self.catalog)
        self.by_id = {i['id']: i for i in self.items}

    def read(self):
        return validate(json.loads(self.path.read_text(encoding='utf-8'))) if self.path.exists() else empty_inventory()

    @contextmanager
    def locked(self):
        import fcntl
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with self.path.with_suffix('.lock').open('a+b') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            try:
                yield
            finally:
                fcntl.flock(lock, fcntl.LOCK_UN)

    def check_revision(self, request, current):
        if request.get('revision') != digest(current):
            raise ValueError('库存已被另一个窗口修改，请重新载入后再操作')

    def write(self, current, document):
        if current == document:
            return
        if self.path.exists():
            stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
            atomic_json(self.path.parent / 'equipment_backups' / (stamp + '.json'), current)
        atomic_json(self.path, document)

    def snapshot(self):
        document = self.read()
        return dict(ok=True, document=document, revision=digest(document), equipment=self.items,
                    sources=self.catalog['sources'], catalogVersion=self.catalog['version'],
                    retrievedAt=self.catalog['retrievedAt'], notes=self.catalog.get('notes', []))

    def command(self, action, request):
        with self.locked():
            current = self.read()
            if action == 'snapshot':
                return self.snapshot()
            if action == 'export':
                return dict(ok=True, document=current, filename='azur-lane-equipment-inventory.json')
            if action == 'save':
                self.check_revision(request, current)
                document = validate(request.get('document'))
                self.write(current, document)
                return self.snapshot()
            preview_path = self.path.with_suffix('.preview.json')
            if action == 'import-preview':
                self.check_revision(request, current)
                document = validate(request.get('document'))
                revision = digest(current)
                preview_id = digest(dict(document=document, revision=revision))
                known = sum(r['quantity'] for r in document['entries'] if r['equipmentId'] in self.by_id)
                unknown = sorted({r['equipmentId'] for r in document['entries'] if r['equipmentId'] not in self.by_id})
                before = {(r['equipmentId'], r['enhancement'], r['locked']): r for r in current['entries']}
                after = {(r['equipmentId'], r['enhancement'], r['locked']): r for r in document['entries']}
                result = dict(ok=True, previewId=preview_id, revision=revision,
                              oldCount=sum(r['quantity'] for r in current['entries']),
                              newCount=sum(r['quantity'] for r in document['entries']),
                              knownCount=known, unknown=unknown,
                              added=len(after.keys() - before.keys()), removed=len(before.keys() - after.keys()),
                              changed=sum(before[k] != after[k] for k in before.keys() & after.keys()))
                atomic_json(preview_path, dict(document=document, result=result))
                return result
            if action == 'import-apply':
                self.check_revision(request, current)
                if not preview_path.exists():
                    raise ValueError('请先预览导入')
                preview = json.loads(preview_path.read_text(encoding='utf-8'))
                result = preview['result']
                if result['revision'] != digest(current) or request.get('previewId') != result['previewId']:
                    raise ValueError('预览已失效，请重新预览导入')
                document = validate(preview['document'])
                if digest(dict(document=document, revision=result['revision'])) != result['previewId']:
                    raise ValueError('预览文件校验失败')
                self.write(current, document)
                preview_path.unlink()
                return self.snapshot()
            raise ValueError('未知的装备操作')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['snapshot', 'save', 'export', 'import-preview', 'import-apply'])
    parser.add_argument('--catalog', required=True)
    parser.add_argument('--user-file', required=True)
    parser.add_argument('--input', required=True)
    args = parser.parse_args()
    try:
        raw = Path(args.input).read_bytes()
        if len(raw) > 4 * 1024 * 1024:
            raise ValueError('库存文件超过 4 MB')
        request = json.loads(raw)
        if not isinstance(request, dict):
            raise ValueError('请求格式无效')
        result = Store(args.user_file, args.catalog).command(args.action, request)
        print(json.dumps(result, ensure_ascii=False))
    except Exception as error:
        print(json.dumps(dict(ok=False, error=str(error)), ensure_ascii=False))
        sys.exit(1)


if __name__ == '__main__':
    main()
