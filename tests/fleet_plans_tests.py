import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('fleets',ROOT/'tools/fleet_plans.py')
f=importlib.util.module_from_spec(spec);spec.loader.exec_module(f)

class FleetStorageTests(unittest.TestCase):
    def test_selective_export(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            path=root/'fleet_plans.json'
            catalog=root/'catalog'
            catalog.mkdir()
            (catalog/'catalog.json').write_text(json.dumps({'ships':[dict(id='bb',baseId='bb',displayName='战列',faction='皇家',type='战列',groups=['后排'])]}))
            path.write_text(json.dumps({'version':1,'plans':[]}))
            inv=root/'commander_cat_inventory.csv'
            inv.write_text('instance_id,cat_id,nickname,initial_talent_ids\nCAT0001,steel,测试,special_fire\n')
            before={p:p.read_bytes() for p in (path,inv)}
            for fleets,inventory in [(True,False),(False,True),(True,True)]:
                result=f.export_data(dict(fleets=fleets,inventory=inventory),catalog,path)
                self.assertEqual('document' in result,fleets)
                self.assertEqual('recommendations' in result,fleets)
                self.assertEqual('ships' in result,fleets)
                self.assertEqual('inventory' in result,inventory)
                self.assertEqual(result['formatVersion'],2 if inventory else 1)
                if inventory:self.assertEqual(result['inventory']['records'][0]['nickname'],'测试')
                if inventory:
                    import base64,io,zipfile
                    with zipfile.ZipFile(io.BytesIO(base64.b64decode(result['download']['base64']))) as archive:
                        if fleets:
                            self.assertEqual(set(archive.namelist()),{'commander_cat_inventory.zip','mac-fleet-plans.json'})
                            self.assertEqual(json.loads(archive.read('mac-fleet-plans.json'))['formatVersion'],1)
                        else:self.assertEqual(set(archive.namelist()),{'commander_cat_inventory.csv','commander_cat_manifest.json'})
            for request in [dict(fleets=False,inventory=False),dict(fleets='true'),dict(inventory=1)]:
                with self.assertRaises(ValueError):f.export_data(request,catalog,path)
            for p,raw in before.items():self.assertEqual(p.read_bytes(),raw)
            path.write_text('{broken')
            self.assertIn('inventory',f.export_data(dict(fleets=False,inventory=True),catalog,path))
            with self.assertRaises(ValueError):f.export_data(dict(fleets=True,inventory=False),catalog,path)
            inv.unlink()
            with self.assertRaisesRegex(ValueError,'不存在'):f.export_data(dict(fleets=False,inventory=True),catalog,path)
    def setUp(self):
        self.catalog={'ships':[dict(id='bb',baseId='bb',groups=['后排'],displayName='战列'),
            dict(id='bb改',baseId='bb',groups=['后排'],displayName='战列改'),
            dict(id='cl',baseId='cl',groups=['前排'],displayName='轻巡')]}
        self.document={'version':1,'plans':[dict(id='opsi',name='大世界',mode='opsi',teams=[
            dict(id='1',name='速刷',kind='surface',goal='speed',slots=['bb','','','cl','',''],priority=1)])]}
    def test_recommendation_staleness_only_used_cats(self):
        facts={'used':dict(catId='a',initial=['fire'])}
        saved={'opsi':dict(ruleVersion=f.FLEET_RULE_VERSION,documentFingerprint=f.fingerprint(self.document),planFingerprint=f.fingerprint(self.document['plans'][0]),usedCats=facts,result={'teams':[]})}
        self.assertFalse(f.recommendation_status(self.document,saved,dict(facts,other={} ))['opsi']['stale'])
        self.assertTrue(f.recommendation_status(self.document,saved,{})['opsi']['stale'])
        self.document['plans'][0]['teams'][0]['priority']=2
        self.assertTrue(f.recommendation_status(self.document,saved,facts)['opsi']['stale'])
    def test_atomic_roundtrip_conflict_corruption(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'fleet_plans.json'
            self.assertEqual(f.load(path,self.catalog),({'version':1,'plans':[]},''))
            f.save(path,self.document,self.catalog,'new')
            original=path.read_bytes()
            self.assertEqual(f.load(path,self.catalog)[0],self.document)
            with self.assertRaises(ValueError): f.save(path,self.document,self.catalog,'new')
            self.assertEqual(path.read_bytes(),original)
            path.write_text('{broken')
            self.assertTrue(f.load(path,self.catalog)[1])
            with self.assertRaises(ValueError): f.save(path,self.document,self.catalog,f.revision(path))
            self.assertEqual(path.read_text(),'{broken')
    def test_slots_and_plan_isolation(self):
        second=json.loads(json.dumps(self.document['plans'][0]));second['id']='campaign';second['mode']='campaign'
        self.document['plans'].append(second)
        self.assertEqual(len(f.validate(self.document,self.catalog)['plans']),2)
        self.document['plans'][0]['teams'][0]['slots'][1]='bb改'
        with self.assertRaisesRegex(ValueError,'重复'): f.validate(self.document,self.catalog)
        self.document['plans'][0]['teams'][0]['slots']=['cl','','','bb','','']
        with self.assertRaisesRegex(ValueError,'不能放'): f.validate(self.document,self.catalog)
    def test_downloaded_catalog(self):
        catalog=f.read_catalog(ROOT/'data/fleet_ships')
        self.assertGreater(len(catalog['ships']),800)
        self.assertFalse(catalog['warnings'])
        u=next(s for s in catalog['ships'] if s['name']=='U-47')
        self.assertEqual(u['displayName'],'U-47（优希娜）')
        for ship in catalog['ships']:
            self.assertTrue(ship['groups'])
            self.assertTrue(ship['faction'])
            self.assertTrue(ship['image'].startswith('/assets/fleet-ships/'))
        import hashlib
        for source in catalog['portraits']:
            image=ROOT/'data/fleet_ships/portraits'/(source['id']+'.jpg')
            # Public checkouts omit third-party portraits; validate any locally cached file.
            if image.exists():
                self.assertEqual(hashlib.sha256(image.read_bytes()).hexdigest(),source['sha256'])

if __name__=='__main__':unittest.main()
