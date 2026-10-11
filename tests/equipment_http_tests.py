"""Exercise equipment endpoints with an isolated save directory and a real server."""
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
    with tempfile.TemporaryDirectory(prefix='game-equipment-http-') as folder:
        root=Path(folder)
        env=dict(os.environ,GAME_GUI_USER_DATA_DIR=str(root/'user_data'),GAME_GUI_CACHE_DIR=str(root/'cache'),GAME_GUI_NO_BROWSER='1')
        proc=subprocess.Popen([executable],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        try:
            assert select.select([proc.stdout],[],[],30)[0], 'server startup timed out'
            line=proc.stdout.readline().decode()
            match=re.search(r'http://127\.0\.0\.1:\d+',line)
            assert match,line
            base=match[0]
            token=None
            def request(path,payload=None,auth=True):
                headers={}
                data=None
                if payload is not None:
                    data=urllib.parse.urlencode({'payload':json.dumps(payload)}).encode()
                    headers['Content-Type']='application/x-www-form-urlencoded'
                    if auth:headers['X-Azur-Token']=token
                try:
                    with urllib.request.urlopen(urllib.request.Request(base+path,data=data,headers=headers),timeout=30) as response:
                        raw=response.read()
                        return response.status,json.loads(raw) if response.headers.get('Content-Type','').startswith('application/json') else raw
                except urllib.error.HTTPError as error:
                    return error.code,json.load(error)
            status,app=request('/api/app/snapshot');assert status==200
            token=app['token']
            for path in ['/','/equipment.js','/equipment.css','/assets/equipment/ranking-119-2025-08-28.jpg']:
                assert request(path)[0]==200,path
            status,initial=request('/api/equipment/snapshot');assert status==200,initial
            assert len(initial['equipment'])>900
            assert not initial['document']['entries']
            original=initial['document']
            item=dict(equipmentId='维修工具T3',name='维修工具T3',quantity=4,enhancement=13,locked=False,note='HTTP 实际保存')
            doc=dict(original,entries=[item])
            assert request('/api/equipment/save',dict(document=doc,revision=initial['revision']),False)[0]==403
            status,saved=request('/api/equipment/save',dict(document=doc,revision=initial['revision']));assert status==200,saved
            assert saved['document']['entries']==[item]
            assert request('/api/equipment/save',dict(document=original,revision=initial['revision']))[0]==400
            status,exported=request('/api/equipment/export',{});assert status==200
            assert exported['document']==saved['document']
            assert request('/api/equipment/import-preview',dict(document=dict(doc,scope='equipped'),revision=saved['revision']))[0]==400
            status,preview=request('/api/equipment/import-preview',dict(document=original,revision=saved['revision']));assert status==200,preview
            assert preview['oldCount']==4 and preview['newCount']==0
            assert request('/api/equipment/snapshot')[1]['document']==saved['document']
            status,applied=request('/api/equipment/import-apply',preview);assert status==200,applied
            assert applied['document']==original
            backups=list((root/'user_data/equipment_backups').glob('*.json'));assert len(backups)==1
            assert json.loads(backups[0].read_text())==saved['document']
            assert request('/api/ship-stories/snapshot')[0]==200
            assert request('/api/commander-cats/snapshot')[0]==200
            assert request('/api/stella-sora/snapshot')[0]==404
            print('Equipment HTTP: save/export/import, auth, revisions, backups and existing modules passed')
        finally:
            proc.terminate()
            proc.wait(timeout=10)


if __name__=='__main__':run(sys.argv[1])
