"""Shared fleet transfer preview/apply; platform-neutral and inventory-preserving."""
import json
import secrets
import csv
from pathlib import Path
import fleet_plans as fleets
from local_transaction import commit


def preview(transfer, catalog_path, user_file, preview_path):
    if transfer.get('format')!='game-assistant-fleet-transfer' or transfer.get('formatVersion')!=1 or transfer.get('ruleVersion')!=fleets.RULE_VERSION or transfer.get('fleetRuleVersion')!=fleets.FLEET_RULE_VERSION:
        raise ValueError('编队规则或格式版本不兼容，请从同版本导出；库存请使用库存导入入口')
    catalog=fleets.read_catalog(catalog_path)
    document=fleets.validate(transfer['document'],catalog)
    refs={sid for p in document['plans'] for t in p['teams'] for sid in t['slots'] if sid}
    incoming={s['id']:s for s in transfer.get('ships',[])}
    local={s['id']:s for s in catalog['ships']}
    if set(incoming)!=refs or len(incoming)!=len(transfer.get('ships',[])) or any(incoming[s]!=local.get(s) for s in refs):
        raise ValueError('舰娘引用资料不一致，请更新图鉴后重试')
    old,warning=fleets.load(user_file,catalog)
    if warning:raise ValueError(warning)
    # Imported snapshots are historical only. They are not silently recalculated.
    saved=transfer.get('recommendations',{})
    if not isinstance(saved,dict):raise ValueError('推荐快照格式错误')
    with (catalog_path.parent/'commander_cats/commander_cat_talents.csv').open(encoding='utf-8-sig',newline='') as stream:
        talent_ids={r['talent_id'] for r in csv.DictReader(stream)}
    for pid,entry in saved.items():
        if pid not in {p['id'] for p in document['plans']} or not isinstance(entry,dict) or not isinstance(entry.get('result',{}).get('teams'),list):raise ValueError('推荐快照无法关联方案')
        allowed={t['id'] for p in document['plans'] if p['id']==pid for t in p['teams']}
        seen=set()
        for team in entry['result']['teams']:
            if not isinstance(team,dict) or team.get('id') not in allowed or team['id'] in seen or type(team.get('complete')) is not bool:raise ValueError('推荐队伍记录无效')
            seen.add(team['id'])
            if not team['complete']:continue
            for role in ('commander','staff'):
                member=team.get(role,{})
                target=member.get('targetTalentIds')
                if not isinstance(member.get('id'),str) or not isinstance(target,list) or not target or any(t not in talent_ids for t in target):raise ValueError('推荐猫或天赋无效')
        entry['catalogStale']=True
    value=dict(id=secrets.token_hex(16),revision=fleets.revision(user_file),recommendationRevision=fleets.revision(user_file.with_name('fleet_recommendations.json')),catalogRevision=fleets.revision(catalog_path/'catalog.json'),document=document,recommendations=saved)
    fleets.atomic_json(preview_path,value)
    return dict(ok=True,previewId=value['id'],oldPlans=len(old['plans']),newPlans=len(document['plans']),message='仅替换编队和历史推荐，不修改库存；导入后请重新推荐全部方案。')


def apply(request,catalog_path,user_file,preview_path):
    value=json.loads(preview_path.read_text(encoding='utf-8'))
    if request.get('previewId')!=value['id']:raise ValueError('预览已失效，请重新预览')
    with fleets.result_lock(user_file.with_name('fleet_recommendations.json')):
        if value['revision']!=fleets.revision(user_file) or value['recommendationRevision']!=fleets.revision(user_file.with_name('fleet_recommendations.json')) or value['catalogRevision']!=fleets.revision(catalog_path/'catalog.json'):raise ValueError('数据已变化，请重新预览')
        fleets.validate(value['document'],fleets.read_catalog(catalog_path))
        changes={user_file:json.dumps(value['document'],ensure_ascii=False,indent=2).encode(),user_file.with_name('fleet_recommendations.json'):json.dumps(value['recommendations'],ensure_ascii=False,indent=2).encode()}
        backup=commit(user_file.parent,changes)
        preview_path.unlink()
        return dict(ok=True,backup=backup)
