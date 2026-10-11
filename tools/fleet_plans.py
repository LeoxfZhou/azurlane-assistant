#!/usr/bin/env python3
"""Versioned local fleet-plan storage. No network or inventory writes."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import tempfile
import csv
import io
import base64
import zipfile
from datetime import datetime, timezone
from contextlib import contextmanager

RULE_VERSION = 'embryo-2026-10-06-v2'
FLEET_RULE_VERSION = 'fleets-2026-10-08-v6-talent-audit'

def export_data(request, catalog_path, user_file):
    fleets = request.get('fleets', True)
    inventory = request.get('inventory', False)
    if type(fleets) is not bool or type(inventory) is not bool or not (fleets or inventory):
        raise ValueError('请至少选择一项导出数据，选项必须为布尔值')
    result = dict(ok=True, format='game-assistant-fleet-transfer', formatVersion=2 if inventory else 1,
        exportedAt=datetime.now(timezone.utc).isoformat(), ruleVersion=RULE_VERSION,
        fleetRuleVersion=FLEET_RULE_VERSION, includedSections=[])
    if fleets:
        catalog = read_catalog(catalog_path)
        document, warning = load(user_file, catalog)
        if warning: raise ValueError(warning)
        saved_path = user_file.with_name('fleet_recommendations.json')
        saved = json.loads(saved_path.read_text(encoding='utf-8')) if saved_path.exists() else {}
        facts = inventory_facts(user_file.with_name('commander_cat_inventory.csv'))
        result.update(document=document, recommendations=recommendation_status(document,saved,facts),
            catalogSha256=revision(catalog_path/'catalog.json'),
            ships=[s for s in catalog['ships'] if any(s['id'] in t['slots'] for p in document['plans'] for t in p['teams'])])
        result['includedSections'].append('fleets')
    if inventory:
        path = user_file.with_name('commander_cat_inventory.csv')
        if not path.exists(): raise ValueError('库存文件不存在，无法导出；不会将缺失文件当作空库存')
        raw = path.read_bytes()
        reader = csv.DictReader(io.StringIO(raw.decode('utf-8-sig'), newline=''))
        fields = ['instance_id','cat_id','nickname','initial_talent_ids']
        if reader.fieldnames != fields: raise ValueError('库存表头不兼容')
        records = list(reader)
        seen = set()
        for row in records:
            if set(row) != set(fields) or any(row[k] is None for k in fields) or not row['instance_id'] or not row['cat_id'] or not row['initial_talent_ids'] or row['instance_id'] in seen:
                raise ValueError('库存记录损坏或实例编号重复，取消导出')
            seen.add(row['instance_id'])
        result['inventory'] = dict(formatVersion=2,ruleVersion=RULE_VERSION,records=records,
            sourceCsvSha256=hashlib.sha256(raw).hexdigest())
        result['includedSections'].append('inventory')
        # Transport the same four-column CSV/manifest accepted by Windows.
        manifest=dict(format_version=2,rule_version=RULE_VERSION,inventory_sha256=hashlib.sha256(raw).hexdigest())
        inner=io.BytesIO()
        with zipfile.ZipFile(inner,'w',zipfile.ZIP_DEFLATED) as archive:
            archive.writestr('commander_cat_inventory.csv',raw)
            archive.writestr('commander_cat_manifest.json',json.dumps(manifest,ensure_ascii=False))
        payload=inner.getvalue()
        filename='commander_cat_inventory.zip'
        if fleets:
            combined=io.BytesIO()
            transfer={key:value for key,value in result.items() if key not in ('inventory','includedSections')}
            transfer['formatVersion']=1
            with zipfile.ZipFile(combined,'w',zipfile.ZIP_DEFLATED) as archive:
                archive.writestr('commander_cat_inventory.zip',payload)
                archive.writestr('mac-fleet-plans.json',json.dumps(transfer,ensure_ascii=False,indent=2))
            payload=combined.getvalue();filename='commander-cat-transfer.zip'
        result['download']=dict(filename=filename,base64=base64.b64encode(payload).decode())
    return result

def fingerprint(value):
    return hashlib.sha256(json.dumps(value, ensure_ascii=False, sort_keys=True).encode()).hexdigest()

def inventory_facts(path):
    if not path.exists(): return {}
    with path.open(encoding='utf-8-sig', newline='') as source:
        return {r['instance_id']:dict(catId=r['cat_id'],initial=sorted(r['initial_talent_ids'].split(' + '))) for r in csv.DictReader(source)}

def recommendation_status(document, saved, inventory):
    output = {}
    for plan in document['plans']:
        entry = saved.get(plan['id'])
        if entry:
            stale = entry.get('ruleVersion') != FLEET_RULE_VERSION or entry.get('documentFingerprint') != fingerprint(document) or entry.get('catalogStale', False) or entry.get('planFingerprint') != fingerprint(plan) or any(inventory.get(k)!=v for k,v in dict(entry.get('usedCats',{}),**entry.get('dependencyCats',{})).items())
            output[plan['id']] = dict(entry, stale=stale)
    return output

def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=path.name+'.',suffix='.tmp',dir=path.parent)
    try:
        with os.fdopen(fd,'w',encoding='utf-8') as stream:
            json.dump(value,stream,ensure_ascii=False,indent=2);stream.flush();os.fsync(stream.fileno())
        os.replace(temporary,path)
    finally:
        if os.path.exists(temporary):os.unlink(temporary)

@contextmanager
def result_lock(path):
    path.parent.mkdir(parents=True,exist_ok=True)
    with path.with_suffix('.lock').open('a+b') as lock:
        if os.name=='nt':
            import msvcrt
            lock.seek(0);lock.write(b'0');lock.flush();lock.seek(0)
            msvcrt.locking(lock.fileno(),msvcrt.LK_LOCK,1)
        else:
            import fcntl
            fcntl.flock(lock.fileno(),fcntl.LOCK_EX)
        try: yield
        finally:
            if os.name=='nt':
                lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_UNLCK,1)
            else: fcntl.flock(lock.fileno(),fcntl.LOCK_UN)

def read_catalog(root):
    result = json.loads((root / 'catalog.json').read_text(encoding='utf-8'))
    ships = result['ships']
    if not ships or len({s['id'] for s in ships}) != len(ships):
        raise ValueError('舰娘图鉴为空或存在重复ID')
    for ship in ships:
        for field in ('id', 'baseId'):
            if not isinstance(ship.get(field), str) or not re.fullmatch(r'[A-Za-z0-9_-]+', ship[field]):
                raise ValueError('图鉴ID无效')
        for field in ('displayName', 'faction', 'type'):
            clean_string(ship.get(field), '图鉴' + field, 120)
        if not isinstance(ship.get('groups'), list) or not ship['groups'] or any(g not in ('前排','后排','潜艇') for g in ship['groups']):
            raise ValueError('图鉴舰娘位置无效')
    bases = {s['baseId'] for s in ships}
    for portrait in result.get('portraits', []):
        if portrait.get('id') not in bases:
            raise ValueError('头像ID无法关联舰娘')
    return result

def clean_string(value, label, maximum=60):
    if not isinstance(value, str) or not value.strip() or len(value) > maximum or any(ord(c)<32 for c in value):
        raise ValueError(label + '无效')
    return value.strip()

def validate(document, catalog):
    if not isinstance(document, dict) or type(document.get('version')) is not int or document['version'] != 1:
        raise ValueError('编队格式版本不兼容')
    plans = document.get('plans')
    if not isinstance(plans, list) or len(plans) > 30: raise ValueError('最多保存30个方案')
    lookup = {s['id']:s for s in catalog['ships']}
    seen_plans = set()
    clean = []
    for plan in plans:
        if not isinstance(plan, dict): raise ValueError('方案记录格式无效')
        key = clean_string(plan.get('id'), '方案ID')
        if key in seen_plans: raise ValueError('方案ID重复')
        seen_plans.add(key)
        mode = plan.get('mode')
        if mode not in ('opsi', 'campaign'): raise ValueError('请选择大世界或主线')
        teams = plan.get('teams')
        if not isinstance(teams, list) or len(teams) > 8: raise ValueError('每个方案最多8队')
        seen_ships, seen_teams, clean_teams = set(), set(), []
        for team in teams:
            if not isinstance(team, dict): raise ValueError('队伍记录格式无效')
            tid = clean_string(team.get('id'), '队伍ID')
            if tid in seen_teams: raise ValueError('队伍ID重复')
            seen_teams.add(tid)
            kind, goal = team.get('kind'), team.get('goal')
            if kind not in ('surface', 'submarine'): raise ValueError('队伍类型无效')
            if goal not in ('damage','survival','speed','torpedo','antisub'): raise ValueError('队伍用途无效')
            slots = team.get('slots')
            if not isinstance(slots, list) or len(slots) != (6 if kind=='surface' else 3):
                raise ValueError('水面队需要6个槽位，潜艇队需要3个槽位；未使用的位置请留空')
            for index, sid in enumerate(slots):
                if sid == '': continue
                if not isinstance(sid, str) or sid not in lookup: raise ValueError('未知舰娘ID，请重新选择')
                ship = lookup[sid]
                expected = '潜艇' if kind=='submarine' else ('后排' if index<3 else '前排')
                if expected not in ship['groups']: raise ValueError(ship['displayName'] + '不能放在' + expected)
                if ship['baseId'] in seen_ships: raise ValueError('同一方案不能重复使用同一舰娘（含改造前后）')
                seen_ships.add(ship['baseId'])
            priority=team.get('priority',len(clean_teams)+1)
            if type(priority) is not int or not 1<=priority<=99: raise ValueError('优先级编号须为1—99')
            clean_teams.append(dict(id=tid,name=clean_string(team.get('name'),'队名'),kind=kind,goal=goal,slots=slots,priority=priority))
        clean.append(dict(id=key,name=clean_string(plan.get('name'),'方案名'),mode=mode,teams=clean_teams))
    return dict(version=1,plans=clean)

def revision(path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.exists() else 'new'

def load(path, catalog):
    if not path.exists(): return dict(version=1,plans=[]), ''
    try: return validate(json.loads(path.read_text(encoding='utf-8')), catalog), ''
    except (ValueError, KeyError, TypeError) as error:
        return dict(version=1,plans=[]), '编队存档损坏或有不兼容数据，原文件已保留，禁止覆盖：' + str(error)

def save(path, document, catalog, expected):
    path.parent.mkdir(parents=True, exist_ok=True)
    # Separate server instances must not pass the same optimistic revision
    # check concurrently. The lock belongs to this exact personal plan file.
    with path.with_suffix('.lock').open('a+b') as lock:
        if os.name == 'nt':
            import msvcrt
            lock.seek(0);lock.write(b'0');lock.flush();lock.seek(0)
            msvcrt.locking(lock.fileno(),msvcrt.LK_LOCK,1)
        else:
            import fcntl
            fcntl.flock(lock.fileno(),fcntl.LOCK_EX)
        try: return save_locked(path, document, catalog, expected)
        finally:
            if os.name == 'nt':
                lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_UNLCK,1)
            else: fcntl.flock(lock.fileno(),fcntl.LOCK_UN)

def save_locked(path, document, catalog, expected):
    _, warning = load(path, catalog)
    if warning: raise ValueError(warning)
    if expected != revision(path): raise ValueError('编队已在另一窗口更新，请重新载入后修改')
    result = validate(document, catalog)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=path.name+'.', suffix='.tmp', dir=path.parent)
    try:
        with os.fdopen(fd, 'w', encoding='utf-8') as output:
            json.dump(result, output, ensure_ascii=False, indent=2)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary): os.unlink(temporary)
    return result

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['snapshot','save','recommend','theory','store-result','export','offline','import-preview','import-apply'])
    parser.add_argument('--catalog', type=Path, required=True)
    parser.add_argument('--user-file', type=Path, required=True)
    parser.add_argument('--input', type=Path)
    parser.add_argument('--team-output', type=Path)
    parser.add_argument('--computed-result', type=Path)
    args = parser.parse_args()
    from local_transaction import recover
    with result_lock(args.user_file.with_name('fleet_recommendations.json')):
        recover(args.user_file.parent)
    request = json.loads(args.input.read_text(encoding='utf-8')) if args.input else {}
    if args.action in ('import-preview','import-apply'):
        import cat_transfer
        preview_path=args.input.with_name('fleet-import-preview.json')
        if args.action=='import-preview':return cat_transfer.preview(request['transfer'],args.catalog,args.user_file,preview_path)
        return cat_transfer.apply(request,args.catalog,args.user_file,preview_path)
    if args.action == 'export':
        return export_data(request, args.catalog, args.user_file)
    catalog = read_catalog(args.catalog)
    document, warning = load(args.user_file, catalog)
    request = json.loads(args.input.read_text(encoding='utf-8')) if args.input else {}
    result_path=args.user_file.with_name('fleet_recommendations.json')
    saved=json.loads(result_path.read_text(encoding='utf-8')) if result_path.exists() else {}
    inventory=inventory_facts(args.user_file.with_name('commander_cat_inventory.csv'))
    if args.action == 'save':
        document = save(args.user_file, request['document'], catalog, request['revision'])
    if args.action in ('recommend','theory'):
        if warning: raise ValueError(warning)
        plan = next((p for p in document['plans'] if p['id']==request.get('planId')), None)
        if plan is None: raise ValueError('方案不存在，请先保存')
        ships = {s['id']:s for s in catalog['ships']}
        lines = []
        for source_plan in ([plan] if args.action=='theory' else document['plans']):
            for team in source_plan['teams']:
                lines.append('\t'.join(['TEAM',team['id'],team['name'],team['kind'],team['goal'],source_plan['mode'],str(team['priority']),source_plan['id']]))
                for slot, sid in enumerate(team['slots']):
                    if sid:
                        ship = ships[sid]
                        fields = ['SHIP',sid,ship['displayName'],ship['faction'],ship['type'],str(slot)]
                        if any(any(c in f for c in '\n\r\t') for f in fields): raise ValueError('图鉴字段含控制字符')
                        lines.append('\t'.join(fields))
        args.team_output.write_text('\n'.join(lines)+'\n', encoding='utf-8')
        atomic_json(args.team_output.with_suffix('.context.json'),dict(plan=plan,document=document,inventory=inventory))
    if args.action == 'store-result':
        if warning: raise ValueError(warning)
        context=json.loads(args.team_output.with_suffix('.context.json').read_text(encoding='utf-8'))
        plan=next((p for p in document['plans'] if p['id']==request.get('planId')),None)
        if document!=context['document'] or inventory!=context['inventory']: raise ValueError('计算期间数据已变化，请重新推荐')
        result=json.loads(args.computed_result.read_text(encoding='utf-8'))
        used={m['id']:inventory[m['id']] for t in result['teams'] if t['complete'] for m in (t['commander'],t['staff'])}
        with result_lock(result_path):
            saved=json.loads(result_path.read_text(encoding='utf-8')) if result_path.exists() else {}
            for source_plan in document['plans']:
                scoped=dict(ok=True,teams=[t for t in result['teams'] if t['planId']==source_plan['id']])
                scoped_used={m['id']:inventory[m['id']] for t in scoped['teams'] if t['complete'] for m in (t['commander'],t['staff'])}
                saved[source_plan['id']]=dict(result=scoped,usedCats=scoped_used,dependencyCats=used,planFingerprint=fingerprint(source_plan),
                    documentFingerprint=fingerprint(document),calculatedAt=datetime.now(timezone.utc).isoformat(),ruleVersion=FLEET_RULE_VERSION)
            atomic_json(result_path,saved)
    recommendations=recommendation_status(document,saved,inventory)
    if args.action == 'offline':
        from cat_offline import summarize
        with (args.catalog.parent/'commander_cats/commander_cat_talents.csv').open(encoding='utf-8-sig',newline='') as stream:
            talents={row['talent_id']:row for row in csv.DictReader(stream)}
        if warning:
            for entry in recommendations.values(): entry['stale']=True
        return summarize(args.user_file.parent,inventory,document,recommendations,talents)
    return dict(ok=True,document=document,revision=revision(args.user_file),warning=warning,
                teams=result['teams'] if args.action=='store-result' else [],
                recommendations=recommendations,ships=catalog['ships'],catalogDate=catalog.get('fetchedAt',''),catalogWarnings=catalog.get('warnings',[]))

if __name__ == '__main__':
    try: print(json.dumps(main(), ensure_ascii=False))
    except Exception as error:
        print(json.dumps(dict(ok=False,error=str(error)), ensure_ascii=False))
        raise SystemExit(1)
