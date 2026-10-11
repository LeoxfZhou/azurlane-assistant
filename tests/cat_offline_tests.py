import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from cat_offline import summarize

class OfflineTests(unittest.TestCase):
    def test_evidence_facts_targets(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            image=b'fixture';digest=hashlib.sha256(image).hexdigest()
            (root/'graduation_images').mkdir();(root/'graduation_images'/(digest+'.png')).write_bytes(image)
            facts={'CAT1':dict(catId='cat',initial=['a'])}
            record=dict(fact=dict(cat_id='cat',initial=['a']),current=['a'],target=['a'],completed_target=['a'],evidence=dict(verified=True,ids=['a'],images={'old.png':digest}))
            (root/'graduation.json').write_text(json.dumps({'CAT1':record,'CAT0':record}))
            (root/'offline_preferences.json').write_text('{"appendGraduationSuffix":true}')
            doc={'plans':[{'id':'p'}]}
            member=dict(id='CAT1',targetTalentIds=['a'])
            rec={'p':dict(stale=False,result={'teams':[dict(complete=True,commander=member,staff=member)]})}
            talents={'a':dict(family_id='f',level='1'),'b':dict(family_id='f',level='2')}
            def get():return summarize(root,facts,doc,rec,talents)
            self.assertEqual(get()['cats']['CAT1']['status'],'graduated')
            self.assertEqual(get()['historicalIds'],['CAT0'])
            self.assertTrue(get()['cats']['CAT1']['appendSuffix'])
            rec['p']['stale']=True;self.assertEqual(get()['cats']['CAT1']['status'],'stale')
            rec['p']['stale']=False;member['targetTalentIds']=['b'];self.assertEqual(get()['cats']['CAT1']['status'],'stale')
            member['targetTalentIds']=['a'];facts['CAT1']['initial']=['b'];self.assertEqual(get()['cats']['CAT1']['status'],'mismatch')
            facts['CAT1']['initial']=['a'];(root/'graduation_images'/(digest+'.png')).write_bytes(b'bad')
            self.assertEqual(get()['cats']['CAT1']['status'],'unknown')

if __name__=='__main__':unittest.main()
