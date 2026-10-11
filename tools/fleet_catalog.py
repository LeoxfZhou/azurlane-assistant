#!/usr/bin/env python3
"""Explicit WIKI refresh for the small offline fleet catalog and portraits.

No personal files are touched. Build in a staging directory, then publish only
after validating the complete catalog. Missing portraits are explicitly listed.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import tempfile
import time
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
API = 'https://wiki.biligame.com/blhx/api.php'

def fetch(url):
    for attempt in range(3):
        try:
            request = urllib.request.Request(url, headers={'User-Agent': 'LocalGameAssistant/2 personal offline catalog'})
            with urllib.request.urlopen(request, timeout=25) as response:
                return response.read()
        except Exception:
            if attempt == 2: raise
            time.sleep(1)

def query(**values):
    return json.loads(fetch(API + '?' + urllib.parse.urlencode(dict(format='json', **values))))

def ask(condition, fields):
    results, offset = {}, 0
    while True:
        response = query(action='ask', query=condition + ''.join('|?' + f for f in fields) + f'|limit=200|offset={offset}')
        batch = response['query']['results']
        if not batch: break
        results.update(batch)
        following = response.get('query-continue-offset')
        if following is None: break
        if int(following) <= offset: raise ValueError('WIKI pagination did not advance')
        offset = int(following)
    return results

def text(row, field):
    values = row['printouts'].get(field, [])
    value = str(values[0]).strip() if values else ''
    if any(c in value for c in '\n\r\t|='): raise ValueError(f'Invalid WIKI field: {field}: {value}')
    return value

def groups(hull):
    if hull in ('潜艇', '潜母', '风帆S'): return ['潜艇']
    if hull in ('驱逐', '轻巡', '重巡', '超巡', '运输', '风帆V'): return ['前排']
    if hull in ('战列', '战巡', '航战', '航母', '正航', '轻航', '重炮', '维修', '风帆M'): return ['后排']
    if hull == '导驱': return ['前排', '后排']
    return []

def build(destination):
    base = ask('[[分类:舰娘||联动舰娘||META舰娘||方案舰娘]]', ['类型','阵营','和谐名','稀有度'])
    # The category also contains administrative pages (e.g. 技能); only
    # actual ship records carry both faction and hull fields.
    base = {name: row for name, row in base.items() if text(row,'类型') and text(row,'阵营')}
    retrofit = ask('[[分类:改造]]', ['改造后类型','阵营','和谐名','改造后稀有度'])
    if len(base) < 500: raise ValueError('Incomplete catalog returned by WIKI')
    titles = sorted(base)
    pageids, images = {}, {}
    for start in range(0, len(titles), 40):
        batch = titles[start:start+40]
        pages = query(action='query', titles='|'.join(batch))['query']['pages']
        for page in pages.values():
            if 'missing' in page: raise ValueError('Missing page: ' + page['title'])
            pageids[page['title']] = page['pageid']
        files = query(action='query', titles='|'.join('文件:' + name + '头像.jpg' for name in batch),
                      prop='imageinfo', iiprop='url', iiurlwidth=80)['query']['pages']
        for page in files.values():
            if page.get('imageinfo'):
                info = page['imageinfo'][0]
                name = page['title'].split(':',1)[1].removesuffix('头像.jpg')
                images[name] = info.get('thumburl', info['url'])
    destination.mkdir(parents=True, exist_ok=True)
    assets = destination / 'portraits'
    assets.mkdir(exist_ok=True)
    records, problems = [], []
    for name in titles:
        if name not in pageids: raise ValueError('Missing stable page identity: ' + name)
        key = 'wiki_' + str(pageids[name])
        for form, row, type_field in [('base', base[name], '类型')] + (
                [('retrofit', retrofit[name], '改造后类型')] if name in retrofit else []):
            hull = text(row, type_field)
            allowed = groups(hull)
            if not allowed: problems.append({'name':name, 'type':hull, 'issue':'unknown placement; not selectable'})
            harmony = text(base[name], '和谐名')
            display = name + ('（' + harmony + '）' if harmony and harmony != name else '')
            if form == 'retrofit': display += '·改'
            records.append(dict(id=key+'_'+form, baseId=key, name=name, harmonyName=harmony,
                displayName=display, faction=text(base[name], '阵营'), type=hull,
                form=form, groups=allowed, source=base[name]['fullurl'],
                image='/assets/fleet-ships/' + key + '.jpg'))
    def portrait(name):
        key = 'wiki_' + str(pageids[name])
        url = images.get(name)
        if not url: return dict(id=key, status='missing')
        path = assets / (key + '.jpg')
        try:
            data = path.read_bytes() if path.exists() else fetch(url)
            if not (data.startswith(b'\xff\xd8') or data.startswith(b'\x89PNG')):
                raise ValueError('not an image')
            if len(data) > 1024*1024: raise ValueError('portrait too large')
            path.write_bytes(data)
            return dict(id=key, status='cached', url=url, sha256=hashlib.sha256(data).hexdigest(), size=len(data))
        except Exception as error:
            return dict(id=key, status='missing', url=url, error=str(error))
    with ThreadPoolExecutor(max_workers=4) as executor:
        sources = list(executor.map(portrait, titles))
    payload = dict(version=1, source='https://wiki.biligame.com/blhx/舰船图鉴',
                   fetchedAt=time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
                   ships=records, portraits=sources, warnings=problems)
    if len({s['id'] for s in records}) != len(records): raise ValueError('duplicate ship identity')
    temporary = destination / 'catalog.json.tmp'
    temporary.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding='utf-8')
    os.replace(temporary, destination / 'catalog.json')
    return dict(ships=len(records), baseShips=len(titles), portraits=sum(x['status']=='cached' for x in sources),
                portraitBytes=sum(x.get('size',0) for x in sources), warnings=problems)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--destination', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(build(args.destination), ensure_ascii=False))
