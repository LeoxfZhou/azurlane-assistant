"""Exercise the real server against isolated personal data, never live saves."""
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import select
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request


def run(executable):
    with tempfile.TemporaryDirectory(prefix="game-cat-v2-") as folder:
        root = Path(folder)
        personal = root / "user_data"
        personal.mkdir(parents=True)
        inventory = personal / "commander_cat_inventory.csv"
        initial = b"instance_id,cat_id,nickname,initial_talent_ids\nCAT0001,oscar,original,special_one_shot + gunnery_main_2 + special_fate\n"
        inventory.write_bytes(initial)
        # An unrelated Mac-only file must survive every import.
        plans = personal / "fleet_plans.json"
        plans.write_text('{"version":1,"plans":[]}', encoding="utf-8")
        original_plans = plans.read_bytes()
        env = dict(os.environ, GAME_GUI_USER_DATA_DIR=str(root / "user_data"),
                   GAME_GUI_CACHE_DIR=str(root / "cache"), GAME_GUI_NO_BROWSER="1")
        process = subprocess.Popen([executable], env=env, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT)
        try:
            if not select.select([process.stdout], [], [], 30)[0]:
                raise AssertionError("GUI startup timed out")
            line = process.stdout.readline().decode()
            match = re.search(r"http://127\.0\.0\.1:\d+", line)
            assert match, line
            base = match.group()
            token = None

            def request(path, values=None, authenticated=True):
                headers = {}
                body = None
                if values is not None:
                    body = urllib.parse.urlencode(values).encode()
                    headers["Content-Type"] = "application/x-www-form-urlencoded"
                    if authenticated:
                        headers["X-Azur-Token"] = token
                req = urllib.request.Request(base + path, data=body, headers=headers)
                try:
                    with urllib.request.urlopen(req, timeout=30) as response:
                        return response.status, json.load(response)
                except urllib.error.HTTPError as error:
                    return error.code, json.load(error)

            status, snapshot = request("/api/commander-cats/snapshot")
            assert status == 200
            token = snapshot["token"]
            status, fleets = request("/api/commander-cats/fleets")
            assert status == 200, fleets
            bb = next(s['id'] for s in fleets['ships'] if s['type']=='战列' and s['form']=='base')
            cl = next(s['id'] for s in fleets['ships'] if s['type']=='轻巡' and s['form']=='base')
            document = dict(version=1, plans=[dict(id='opsi',name='大世界',mode='opsi',teams=[
                dict(id='one',name='混编',kind='surface',goal='damage',slots=[bb,'','',cl,'',''])]),
                dict(id='campaign',name='主线',mode='campaign',teams=[
                dict(id='one',name='主线队',kind='surface',goal='survival',slots=[bb,'','',cl,'',''])])])
            status, saved_fleets = request('/api/commander-cats/fleets/save',
                {'payload':json.dumps(dict(document=document,revision=fleets['revision']))})
            assert status == 200, saved_fleets
            original_plans = plans.read_bytes()
            cat = snapshot["inventory"][0]
            assert cat["embryoUses"] and cat["nickname"] == "original"
            assert cat["ruleVersion"] == "embryo-2026-10-06-v2"
            assert inventory.read_bytes() == initial
            settings = personal / "commander_cat_settings.csv"
            settings_before = settings.read_bytes() if settings.exists() else None
            status, _ = request("/api/commander-cats/import-preview", {}, False)
            assert status == 403

            updated = b"instance_id,cat_id,nickname,initial_talent_ids\nCAT0001,oscar,Windows,special_fire + special_one_shot + gunnery_main_1\nCAT0002,takemaru,Windows2,special_torpedo_soul + special_fate + gunnery_cruiser_1\n"
            manifest = dict(format_version=2, rule_version=cat["ruleVersion"],
                            inventory_sha256=hashlib.sha256(updated).hexdigest())

            def upload(metadata):
                files = [("commander_cat_inventory.csv", updated),
                         ("commander_cat_manifest.json", json.dumps(metadata).encode())]
                values = {"file_count": "2"}
                for index, (name, content) in enumerate(files):
                    values[f"file{index}_name"] = name
                    values[f"file{index}_data"] = base64.b64encode(content).decode()
                return request("/api/commander-cats/import-preview", values)

            status, preview = upload(manifest)
            assert status == 200, preview
            assert preview["includesInventory"] and not preview["includesSettings"]
            assert inventory.read_bytes() == initial, "preview must not write inventory"
            status, result = request("/api/commander-cats/import-apply", {"import_id": preview["importId"]})
            assert status == 200, result
            _, snapshot = request("/api/commander-cats/snapshot")
            cat = snapshot["inventory"][0]
            assert cat["nickname"] == "Windows" and cat["feasibilityTier"] == "S"
            assert cat["protectedRare"] and "S" in cat["batchNickname"]
            status, conflict = request('/api/commander-cats/fleets/recommend',
                {'payload':json.dumps(dict(planId='opsi'))})
            assert status == 400 and '同优先级' in conflict['error'], conflict
            _, current = request('/api/commander-cats/fleets')
            next(p for p in current['document']['plans'] if p['id']=='campaign')['teams'][0]['priority']=2
            status, _ = request('/api/commander-cats/fleets/save',
                {'payload':json.dumps(dict(document=current['document'],revision=current['revision']))})
            assert status == 200
            original_plans = plans.read_bytes()
            for plan_id in ['opsi','campaign']:
                status, recommendation = request('/api/commander-cats/fleets/recommend',
                    {'payload':json.dumps(dict(planId=plan_id))})
                assert status == 200, recommendation
                pair = recommendation['recommendations'][plan_id]['result']['teams'][0]
                assert pair['complete'], pair
                assert pair['commander']['id'] != pair['staff']['id']
                assert pair['commander']['catId'] != pair['staff']['catId']
                assert pair['commander']['points'] <= 6 and pair['staff']['points'] <= 6
                assert pair['commander']['target'] and pair['commander']['reasons']
            recommendations_file=personal/'fleet_recommendations.json'
            persisted=recommendations_file.read_bytes()
            _, reloaded=request('/api/commander-cats/fleets')
            assert set(reloaded['recommendations'])=={'opsi','campaign'}
            assert not reloaded['recommendations']['opsi']['stale']
            status, exported=request('/api/commander-cats/fleets/export',{'payload':'{}'})
            assert status==200 and exported['formatVersion']==1
            assert exported['recommendations']==reloaded['recommendations']
            assert len(exported['ships'])==2
            status, theory=request('/api/commander-cats/fleets/theory',{'payload':json.dumps({'planId':'opsi'})})
            assert status==200 and theory['teams'][0]['complete'],theory
            assert len(theory['teams'][0]['commander']['targetTalentIds'])==5
            assert recommendations_file.read_bytes()==persisted,'theory must not replace saved inventory pair'
            # Unused additions and nicknames are not configuration changes.
            inventory.write_bytes(updated+b'CAT0099,lady,unused,special_new_star\n')
            _, after=request('/api/commander-cats/fleets')
            assert not after['recommendations']['opsi']['stale']
            inventory.write_bytes(updated.replace(b'Windows,',b'renamed,'))
            _, after=request('/api/commander-cats/fleets')
            assert not after['recommendations']['opsi']['stale']
            inventory.write_bytes(updated.replace(b'gunnery_main_1',b'gunnery_main_2'))
            _, after=request('/api/commander-cats/fleets')
            assert after['recommendations']['opsi']['stale']
            assert recommendations_file.read_bytes()==persisted
            inventory.write_bytes(updated)
            saved = inventory.read_bytes()
            status, _ = upload(dict(manifest, rule_version="legacy"))
            assert status >= 400
            status, _ = request("/api/commander-cats/import-apply", {"import_id": preview["importId"]})
            assert status >= 400, "failed preview must invalidate stale pending import"
            assert inventory.read_bytes() == saved and plans.read_bytes() == original_plans
            assert (settings.read_bytes() if settings.exists() else None) == settings_before
            print("Isolated HTTP grading/import checks passed")
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


if __name__ == "__main__":
    run(sys.argv[1])
