"""Verify the standalone server with temporary saves and no browser/network refresh."""
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


def start(executable, directory):
    env = dict(os.environ, GAME_GUI_NO_BROWSER='1',
               GAME_GUI_USER_DATA_DIR=str(directory / 'user_data'),
               GAME_GUI_CACHE_DIR=str(directory / 'cache'))
    process = subprocess.Popen([executable], cwd=directory, env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        assert select.select([process.stdout], [], [], 30)[0], 'startup timed out'
        line = process.stdout.readline().decode()
        match = re.search(r'http://127\.0\.0\.1:\d+', line)
        assert match, line
        return process, match[0]
    except BaseException:
        process.terminate()
        process.wait(timeout=10)
        raise


def request(base, path, values=None, token=None):
    data = None if values is None else urllib.parse.urlencode(values).encode()
    headers = {} if token is None else {'X-Azur-Token': token}
    try:
        with urllib.request.urlopen(urllib.request.Request(base+path, data=data, headers=headers), timeout=30) as response:
            raw = response.read()
            return response.status, json.loads(raw) if 'application/json' in response.headers.get('Content-Type','') else raw
    except urllib.error.HTTPError as error:
        return error.code, error.read()


def run(executable):
    with tempfile.TemporaryDirectory(prefix='standalone-game-') as folder:
        directory = Path(folder)
        process, base = start(executable, directory)
        try:
            status, app = request(base, '/api/app/snapshot')
            assert status == 200
            azur = app['game'] == 'azur_lane'
            _, page = request(base, '/')
            page = page.decode()
            assert ('page-stella-sora' not in page) if azur else ('page-azur-lane' not in page)
            for asset in re.findall(r'(?:src|href)="(/[^"#]+)"', page):
                assert request(base, asset)[0] == 200, asset
            other = '/api/stella-sora/snapshot' if azur else '/api/commander-cats/snapshot'
            assert request(base, other)[0] == 404
            foreign = 'stella' if azur else 'ships'
            assert request(base, '/api/data-refresh/preview', {'module':foreign}, app['token'])[0] == 400
            if azur:
                status, placeholder = request(base, '/assets/fleet-ships/nonexistent-public-test.jpg')
                assert status == 200 and b'<svg' in placeholder
                _, snapshot = request(base, '/api/ship-stories/snapshot')
                entry = snapshot['ships'][0]
                endpoint='/api/ship-stories/progress'
                values={'ship_id':entry['id'], 'status':'completed', 'is_max_level':'true'}
            else:
                _, snapshot = request(base, '/api/stella-sora/snapshot')
                entry = snapshot['trekkers'][0]
                endpoint='/api/stella-sora/roster'
                values={'trekker_id':entry['id'], 'owned':'true', 'level':'71'}
                assert request(base, '/api/stella-sora/emblems')[0] == 200
                assert request(base, '/api/stella-sora/preferences', {'language':'en'}, app['token'])[0] == 200
                status, plans = request(base, '/api/stella-sora/solve-current', {}, app['token'])
                assert status == 200 and 'plans' in plans
            assert request(base, endpoint, values)[0] == 403
            assert request(base, endpoint, values, app['token'])[0] == 200
            assert not (directory/'user_data/azur_lane').exists()
            assert not (directory/'user_data/stella_sora').exists()
        finally:
            process.terminate()
            process.wait(timeout=10)
        process, base = start(executable, directory)
        try:
            path = '/api/ship-stories/snapshot' if azur else '/api/stella-sora/snapshot'
            _, snapshot = request(base, path)
            saved = next(x for x in snapshot['ships' if azur else 'trekkers'] if x['id']==entry['id'])
            if azur:
                assert saved['status']=='completed' and saved['maxLevel']
            else:
                assert saved['owned'] and saved['level']==71
                assert snapshot['preferences']['language']=='en'
        finally:
            process.terminate()
            process.wait(timeout=10)
    print('Standalone routes, assets, auth, save/restart and foreign-module isolation passed')


if __name__ == '__main__':
    run(sys.argv[1])
