#!/usr/bin/env python3

import csv
import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "refresh_game_data.py"
spec = importlib.util.spec_from_file_location("refresh_game_data", SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

sample = "{{舰船资料\r\n|名称=测试舰\r\n|和谐名=\r\n|英文名=Test Ship\r\n}}"
assert module.field(sample, "和谐名") == ""
assert module.field(sample, "英文名") == "Test Ship"

catalog = ROOT / "data" / "ship_stories" / "ship_stories.csv"
with catalog.open(encoding="utf-8-sig", newline="") as source:
    rows = list(csv.DictReader(source))

assert len(rows) == 123
assert all(row["canonical_name"] for row in rows)
assert all("|" not in row["harmony_name"] and "=" not in row["harmony_name"] for row in rows)
u47 = next(row for row in rows if row["canonical_name"] == "U-47")
assert u47["harmony_name"] == "优希娜"
print("ship name refresh data tests passed")

# Refresh must stage, validate and roll back without touching personal files.
import contextlib
import io
import json
import sys
import tempfile
from unittest.mock import patch
sys.path.insert(0, str(ROOT / 'tools'))
import fleet_catalog

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    current = root / 'data/fleet_ships'
    current.mkdir(parents=True)
    def fixture(name):
        return dict(ships=[dict(id='wiki_1_base',baseId='wiki_1',displayName=name,
                               faction='白鹰',type='战列',groups=['后排'])],portraits=[])
    (current/'catalog.json').write_text(json.dumps(fixture('旧名')))
    personal = root/'user_data/fleet_plans.json'
    personal.parent.mkdir();personal.write_text('personal sentinel')
    def build_fake(destination):
        destination.mkdir(parents=True)
        (destination/'portraits').mkdir()
        (destination/'catalog.json').write_text(json.dumps(fixture('新名')))
        return {}
    def run(action):
        with patch.object(module,'ROOT',root), patch.object(sys,'argv',['refresh',action,'fleets']), contextlib.redirect_stdout(io.StringIO()) as output:
            module.main()
        return json.loads(output.getvalue())
    with patch.object(fleet_catalog,'build',build_fake):
        preview=run('prepare')
    assert preview['modules']['fleets']['recordDiff']['modified']==['新名']
    assert json.loads((current/'catalog.json').read_text())['ships'][0]['displayName']=='旧名'
    original_replace=module.os.replace
    def fail_publish(source,target):
        if Path(source).name=='data': raise OSError('simulated publication failure')
        return original_replace(source,target)
    with patch.object(module.os,'replace',fail_publish):
        try: run('apply');raise AssertionError('must fail')
        except OSError: pass
    assert json.loads((current/'catalog.json').read_text())['ships'][0]['displayName']=='旧名'
    run('apply')
    assert json.loads((current/'catalog.json').read_text())['ships'][0]['displayName']=='新名'
    assert personal.read_text()=='personal sentinel'
print('fleet catalog staged refresh and rollback tests passed')
