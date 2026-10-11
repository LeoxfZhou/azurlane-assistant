import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import cat_transfer as transfer
import fleet_plans as fleets
from local_transaction import commit,recover

class TransferTests(unittest.TestCase):
    def test_preview_apply_and_conflict(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);catalog=root/'data/fleet_ships';catalog.mkdir(parents=True)
            (catalog.parent/'commander_cats').mkdir()
            (catalog.parent/'commander_cats/commander_cat_talents.csv').write_text('talent_id\na\n')
            (catalog/'catalog.json').write_text(json.dumps({'ships':[dict(id='ship',baseId='ship',displayName='船',faction='皇家',type='战列',groups=['后排'])]}))
            user=root/'user';user.mkdir();path=user/'fleet_plans.json';path.write_text('{"version":1,"plans":[]}')
            inv=user/'commander_cat_inventory.csv';inv.write_text('untouched')
            value=dict(format='game-assistant-fleet-transfer',formatVersion=1,ruleVersion=fleets.RULE_VERSION,fleetRuleVersion=fleets.FLEET_RULE_VERSION,document={'version':1,'plans':[dict(id='p',name='测试',mode='opsi',teams=[])]},recommendations={},ships=[])
            preview=root/'preview.json'
            result=transfer.preview(value,catalog,path,preview)
            self.assertEqual(json.loads(path.read_text())['plans'],[])
            applied=transfer.apply(dict(previewId=result['previewId']),catalog,path,preview)
            self.assertTrue(Path(applied['backup']).exists());self.assertEqual(inv.read_text(),'untouched')
            self.assertEqual(len(json.loads(path.read_text())['plans']),1)
            result=transfer.preview(value,catalog,path,preview)
            path.write_text('{"changed":true}')
            with self.assertRaisesRegex(ValueError,'变化'):transfer.apply(dict(previewId=result['previewId']),catalog,path,preview)
            value['fleetRuleVersion']='old'
            with self.assertRaisesRegex(ValueError,'版本'):transfer.preview(value,catalog,path,preview)
    def test_transaction_boundary_and_recovery(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)/'user';root.mkdir();p=root/'save.json';p.write_bytes(b'old')
            with self.assertRaises(ValueError):commit(root,{root.parent/'outside':b'bad'})
            backup=commit(root,{p:b'new'});self.assertEqual(p.read_bytes(),b'new')
            journal=json.loads((Path(backup)/'manifest.json').read_text());journal['status']='pending'
            (root/'sync-transaction.json').write_text(json.dumps(journal));recover(root)
            self.assertEqual(p.read_bytes(),b'old')

if __name__=='__main__':unittest.main()
