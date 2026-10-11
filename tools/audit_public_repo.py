#!/usr/bin/env python3
"""Audit the exact Git index without printing private contents or credentials."""
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BLOCKED = {'user_data', 'cache', 'exports', 'input', 'migration', '.personal-sync',
           '.idea', '.vscode', '__pycache__', 'graduation_images', 'sync_backups',
           'import_backups', 'build'}
SUFFIXES = {'.png', '.jpg', '.jpeg', '.webp', '.gif', '.pdf', '.xlsx', '.xls',
            '.zip', '.gz', '.tar', '.pem', '.key', '.pyc', '.log', '.lock'}
PRIVATE_FILES = {'commander_cat_inventory.csv', 'commander_cat_instance_sequence.txt',
                 'ship_story_progress.csv', 'fleet_plans.json', 'fleet_recommendations.json',
                 'graduation.json', 'graduation_image_index.json', 'offline_preferences.json'}
SECRET = re.compile(rb'gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,}'
                    rb'|-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----')


def main():
    names = subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).split(b'\0')
    failures = []
    total = 0
    for raw in filter(None, names):
        name = raw.decode('utf-8')
        path = Path(name)
        if (any(p in BLOCKED or p.startswith(('cmake-build-', 'build.previous.', '.migration-backup'))
                for p in path.parts) or path.suffix.lower() in SUFFIXES
                or path.name in PRIVATE_FILES or path.name.startswith('.env')):
            failures.append((name, 'private/generated/third-party asset path'))
        content = subprocess.check_output(['git', 'show', ':' + name], cwd=ROOT)
        total += len(content)
        if SECRET.search(content):
            failures.append((name, 'credential pattern'))
        if len(content) > 50 * 1024 * 1024:
            failures.append((name, 'large file'))
        if re.search(rb'/Users/[A-Za-z0-9._-]+/', content):
            failures.append((name, 'personal path or email'))
    for name, reason in failures:
        print(f'REJECT: {name}: {reason}')
    print(f'Index audit: {sum(bool(n) for n in names)} files, {total} bytes, {len(failures)} findings')
    return bool(failures)


if __name__ == '__main__':
    sys.exit(main())
