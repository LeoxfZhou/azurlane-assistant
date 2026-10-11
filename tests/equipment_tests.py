"""Warehouse integrity, guide chronology, and research-chain regression tests."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from equipment_store import Store, validate, empty_inventory, digest, evaluated

CATALOG = ROOT / 'data/equipment/catalog.json'


def row(eid='维修工具T3', quantity=1, level=0):
    return dict(equipmentId=eid, name=eid, quantity=quantity, enhancement=level, locked=False, note='')


def document(*rows):
    return dict(empty_inventory(), entries=list(rows))


class EquipmentTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name) / 'equipment_inventory.json'
        self.store = Store(self.path, CATALOG)

    def save(self, doc):
        return self.store.command('save', dict(revision=digest(self.store.read()), document=doc))

    def test_roundtrip_unicode_variants_and_unknown(self):
        doc = document(row('双联装113mm高射炮T3', 4), row('双联装113mm高射炮T2', 2, 13), row('尚未收录的装备T0', 3, None))
        self.save(doc)
        exported = self.store.command('export', {})['document']
        preview = self.store.command('import-preview', dict(document=exported, revision=digest(exported)))
        self.assertEqual(preview['unknown'], ['尚未收录的装备T0'])
        result = self.store.command('import-apply', preview)
        self.assertEqual(result['document'], validate(doc))
        self.assertEqual(sum(r['quantity'] for r in result['document']['entries']), 9)

    def test_stale_save_does_not_overwrite(self):
        old = digest(self.store.read())
        self.save(document(row(quantity=4)))
        before = self.path.read_bytes()
        with self.assertRaisesRegex(ValueError, '另一个窗口'):
            self.store.command('save', dict(revision=old, document=document(row(quantity=99))))
        self.assertEqual(before, self.path.read_bytes())

    def test_backup_and_remove_only_local_record(self):
        first = self.save(document(row(quantity=5)))['document']
        self.save(document())
        backups = list(self.path.parent.glob('equipment_backups/*.json'))
        self.assertEqual(len(backups), 1)
        self.assertEqual(json.loads(backups[0].read_text()), first)
        self.assertEqual(self.store.read()['entries'], [])

    def test_invalid_import_never_changes_save(self):
        self.save(document(row()))
        before = self.path.read_bytes()
        bad_documents = [dict(document(), schemaVersion=2), dict(document(), scope='equipped'),
                         document(row(quantity=-1)), document(row(quantity=True)),
                         document(row(level=14)), document(row(),row()), None,
                         dict(document(), format='another-game')]
        for bad in bad_documents:
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                self.store.command('import-preview', dict(document=bad, revision=digest(self.store.read())))
            self.assertEqual(before, self.path.read_bytes())

    def test_import_preview_can_be_cancelled_without_writing(self):
        current = self.store.read()
        result = self.store.command('import-preview', dict(document=document(row(quantity=4)), revision=digest(current)))
        self.assertEqual(result['newCount'], 4)
        self.assertFalse(self.path.exists())
        self.assertEqual(self.store.read(), current)

    def test_changed_inventory_invalidates_preview(self):
        preview = self.store.command('import-preview', dict(document=document(row(quantity=8)), revision=digest(self.store.read())))
        self.save(document(row(quantity=2)))
        with self.assertRaises(ValueError):
            self.store.command('import-apply', preview)
        self.assertEqual(self.store.read()['entries'][0]['quantity'], 2)

    def test_modified_preview_rejected(self):
        preview = self.store.command('import-preview', dict(document=document(row(quantity=8)), revision=digest(self.store.read())))
        path = self.path.with_suffix('.preview.json')
        staged = json.loads(path.read_text())
        staged['document']['entries'][0]['quantity'] = 99
        path.write_text(json.dumps(staged))
        with self.assertRaisesRegex(ValueError, '校验失败'):
            self.store.command('import-apply', preview)
        self.assertFalse(self.path.exists())

    def test_imported_ratings_and_protection_cannot_override_reference(self):
        imported = dict(row(), grade='D', classification='normal')
        self.save(document(imported))
        self.assertNotIn('grade', self.store.read()['entries'][0])
        self.assertEqual(self.store.by_id['维修工具T3']['grade'], 'A')

    def test_corrupted_save_is_not_silently_reset(self):
        self.path.write_text('{broken')
        with self.assertRaises(json.JSONDecodeError):
            self.store.command('snapshot', {})
        self.assertEqual(self.path.read_text(), '{broken')

    def test_latest_explicit_guide_and_absence_does_not_demote(self):
        catalog = dict(equipment=[dict(id='a',name='a'),dict(id='b',name='b')],sources=[dict(id='old',date='2024-01-01'),dict(id='new',date='2025-01-01')],assessments=[
            dict(equipmentId='a',grade='C',sourceId='new'),dict(equipmentId='a',grade='S',sourceId='old'),dict(equipmentId='b',grade='A',sourceId='old')])
        result = {r['id']:r for r in evaluated(catalog)}
        self.assertEqual(result['a']['grade'],'C')
        self.assertEqual(result['b']['grade'],'A')
        self.assertEqual([r['grade'] for r in result['a']['history']],['S','C'])

    def test_research_multistep_cycle_and_weak_target(self):
        catalog = dict(equipment=[dict(id='a',name='a',researchTo=['b']),dict(id='b',name='b',researchTo=['c']),dict(id='c',name='c',researchTo=['a']),dict(id='d',name='d',researchTo=['e']),dict(id='e',name='e')],sources=[dict(id='s',date='2025-01-01')],assessments=[dict(equipmentId='a',grade='D',sourceId='s'),dict(equipmentId='c',grade='S',sourceId='s'),dict(equipmentId='e',grade='C',sourceId='s')])
        result={r['id']:r for r in evaluated(catalog)}
        self.assertTrue(result['a']['usefulMaterial'])
        self.assertEqual(result['a']['grade'],'D')
        self.assertFalse(result['d']['usefulMaterial'])
        self.assertEqual(len(result['a']['researchProducts']),2)

    def test_reference_integrity_and_special_rules(self):
        catalog=self.store.catalog
        names=[g['id'] for g in catalog['equipment']]
        self.assertEqual(len(names),len(set(names)))
        self.assertGreater(len(names),900)
        for assessment in catalog['assessments']:
            self.assertIn(assessment['equipmentId'],names)
        for item in catalog['equipment']:
            for target in item['researchTo']:
                self.assertIn(target,names)
        self.assertEqual(self.store.by_id['高性能舵机T0']['classification'],'normal')
        for name in ['嗡嗡倾听君T0','小海狸中队队徽T0','双联装138.6mm主炮Mle1934T0']:
            self.assertEqual(self.store.by_id[name]['classification'],'special')
        self.assertEqual(self.store.by_id['双联装113mm高射炮T2']['grade'],'C')
        self.assertEqual(self.store.by_id['双联装113mm高射炮T3']['grade'],'A')
        self.assertTrue(self.store.by_id['双联装128mmSKC41高平两用炮T3']['usefulMaterial'])


if __name__ == '__main__':
    unittest.main()
