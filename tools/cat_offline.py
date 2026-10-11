"""Read-only graduation observations; no emulator imports and no inventory writes."""
import hashlib
import json
import re
from pathlib import Path


def summarize(root, inventory, document, recommendations, talents):
    root = Path(root)
    observations = json.loads((root/'graduation.json').read_text(encoding='utf-8')) if (root/'graduation.json').exists() else {}
    prefs = json.loads((root/'offline_preferences.json').read_text(encoding='utf-8')) if (root/'offline_preferences.json').exists() else {}
    targets, stale = {}, set()
    for plan in document['plans']:
        entry = recommendations.get(plan['id'], {})
        for team in entry.get('result', {}).get('teams', []):
            if not team.get('complete'): continue
            for role in ('commander', 'staff'):
                member = team[role]
                key = member['id']
                target = sorted(member['targetTalentIds'])
                if key in targets and targets[key] != target: stale.add(key)
                targets[key] = target
                if entry.get('stale'): stale.add(key)

    def chains(ids):
        result = {}
        if not isinstance(ids, list) or not ids: raise ValueError('天赋为空')
        for key in ids:
            talent = talents[key]
            family = talent['family_id']
            if family in result: raise ValueError('同成长链重复')
            result[family] = int(talent['level'])
        if len(result) > 5: raise ValueError('超过五槽')
        return result

    output = {}
    for key, fact in inventory.items():
        record = observations.get(key)
        if not record:
            output[key] = dict(status='unknown', label='未观测', images=[], current=[])
            continue
        value = dict(status='unknown', label='待复核', current=record.get('current', []),
                     historicalTarget=record.get('target', []), completedTarget=record.get('completed_target', []), images=[])
        output[key] = value
        if record.get('fact') != dict(cat_id=fact['catId'], initial=fact['initial']):
            value.update(status='mismatch', label='库存事实不匹配'); continue
        evidence = record.get('evidence', {})
        valid = evidence.get('verified') is True and sorted(evidence.get('ids', [])) == sorted(value['current'])
        images = evidence.get('images', {})
        valid = valid and bool(images)
        for digest in images.values():
            if not isinstance(digest, str) or not re.fullmatch('[0-9a-f]{64}', digest): valid=False; continue
            path=root/'graduation_images'/(digest+'.png')
            if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest()!=digest: valid=False; continue
            value['images'].append('/assets/cat-evidence/'+digest+'.png')
        if not valid: value['label']='证据缺失或不匹配'; continue
        try:
            have=chains(value['current'])
            target=targets.get(key, record.get('target', []))
            wanted=chains(target)
        except (ValueError, KeyError, TypeError):
            value['label']='天赋记录无效'; continue
        if key in stale or sorted(target)!=sorted(record.get('target', [])):
            value.update(status='stale', label='目标已变化，待复核'); continue
        complete=all(have.get(family,0)>=level for family,level in wanted.items())
        value.update(status='graduated' if complete else 'growing',
                     label=('历史观测已毕业' if complete else '历史观测培养中')+('（未选队）' if key not in targets else ''),
                     appendSuffix=complete and prefs.get('appendGraduationSuffix',False) is True)
    return dict(ok=True,cats=output,historicalIds=sorted(set(observations)-set(inventory)),
                notice='Windows历史观测，不代表实时游戏状态；当前培养不影响初始天赋评级。')
