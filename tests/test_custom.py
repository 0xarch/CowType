# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor

ROOT = Path(__file__).resolve().parents[1]
def module(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    result = importlib.util.module_from_spec(spec)
    sys.modules[name] = result
    spec.loader.exec_module(result)
    return result
store = module('custom_test_store', 'src-settings/config/custom.py')
settings = module('custom_test_settings', 'src-settings/config/settings.py')

class CustomTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.record = store.Record('abc', '第一行\n第二行\t\\路径 <literal>\u2028\u2029', 100)
    def change(self, old, new):
        store.change(self.root, settings.atomic, old, new)
    def test_roundtrip_autosave_update_and_delete(self):
        self.assertEqual(store.load(self.root), [])
        self.change(None, self.record)
        self.assertEqual(store.load(self.root), [self.record])
        self.assertEqual((self.root / 'custom-v1.tsv').stat().st_mode & 0o777, 0o600)
        new = store.Record('xyz', self.record.text, 500)
        self.change(self.record, new)
        self.assertEqual(store.load(self.root), [new])
        self.change(new, None)
        self.assertEqual(store.load(self.root), [])
    def test_conflict_duplicate_and_invalid_do_not_overwrite(self):
        self.change(None, self.record)
        original = (self.root / 'custom-v1.tsv').read_bytes()
        for old, new in [(None, self.record), (store.Record('abc','missing',1), None), (self.record, store.Record('a1','字',1)), (self.record,store.Record('abc','字',1000001)), (self.record,store.Record('abc','字\x00',1))]:
            with self.assertRaises(ValueError): self.change(old,new)
            self.assertEqual((self.root / 'custom-v1.tsv').read_bytes(),original)
    def test_concurrent_adds_preserve_each_other(self):
        records = [store.Record('test',f'合成{i}',i+1) for i in range(20)]
        with ThreadPoolExecutor(max_workers=4) as pool:
            list(pool.map(lambda r:self.change(None,r),records))
        self.assertEqual(set(store.load(self.root)),set(records))
    def test_corrupt_or_symlink_file_preserved(self):
        path = self.root / 'custom-v1.tsv'
        path.write_text('unknown')
        with self.assertRaises(ValueError): self.change(None,self.record)
        self.assertEqual(path.read_text(),'unknown')
        path.unlink()
        path.symlink_to(self.root / 'missing')
        with self.assertRaises(ValueError): self.change(None,self.record)
    def test_validation_boundaries(self):
        for record in [store.Record('A','字',1),store.Record('','字',1),store.Record('a'*65,'字',1),store.Record('abc','  ',1),store.Record('abc','字'*4097,1),store.Record('abc','字',True)]:
            self.assertFalse(store.valid(record))
        self.assertTrue(store.valid(store.Record('a'*64,'字'*4096,1000000)))
