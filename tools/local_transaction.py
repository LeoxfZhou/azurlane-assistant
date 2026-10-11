"""Recoverable local file transactions. Call under the fleet store lock."""
import json
import os
import shutil
import tempfile
from pathlib import Path
from datetime import datetime


def atomic_bytes(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(dir=path.parent, prefix=path.name+'.')
    try:
        with os.fdopen(fd, 'wb') as out:
            out.write(data); out.flush(); os.fsync(out.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary): os.unlink(temporary)


def recover(root):
    root=Path(root).resolve()
    journal = root/'sync-transaction.json'
    if not journal.exists(): return
    value = json.loads(journal.read_text(encoding='utf8'))
    for row in value['files']:
        if not Path(row['target']).resolve().is_relative_to(root) or not Path(row['backup']).resolve().is_relative_to(root/'sync_backups'):
            raise ValueError('事务日志含越界路径，保留文件并停止恢复')
    if value['status'] == 'pending':
        for row in value['files']:
            target = Path(row['target'])
            if row['existed']: atomic_bytes(target, Path(row['backup']).read_bytes())
            elif target.exists(): target.unlink()
    journal.unlink()


def commit(root, changes):
    root = Path(root).resolve()
    if any(not Path(path).resolve().is_relative_to(root) for path in changes):
        raise ValueError('事务目标必须位于个人数据目录内')
    recover(root)
    backup = root/'sync_backups'/datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    backup.mkdir(parents=True)
    rows = []
    for index, (path, data) in enumerate(changes.items()):
        path = Path(path).resolve()
        saved = backup/str(index)
        if path.exists(): shutil.copy2(path, saved)
        rows.append(dict(target=str(path), backup=str(saved.resolve()), existed=path.exists()))
    journal = root/'sync-transaction.json'
    atomic_bytes(journal, json.dumps(dict(status='pending',files=rows)).encode())
    try:
        for path, data in changes.items(): atomic_bytes(path, data)
        atomic_bytes(journal, json.dumps(dict(status='complete',files=rows)).encode())
        shutil.copy2(journal, backup/'manifest.json')
        journal.unlink()
    except BaseException:
        recover(root)
        raise
    return str(backup)
