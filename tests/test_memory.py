# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
from pathlib import Path
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
def module(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    result = importlib.util.module_from_spec(spec)
    sys.modules[name] = result
    spec.loader.exec_module(result)
    return result
memory = module('memory_test_store', 'src-settings/config/memory.py')
settings = module('memory_test_settings', 'src-settings/config/settings.py')

class MemoryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.path = self.root / 'learning-v1.tsv'
        self.now = int(time.time())
        self.original = f'# cowtype-learning-v1\n# reset-token\ttoken\nshan\t杉\t8\t{self.now}\nshan\t山\t2\t{self.now}\n'
        settings.atomic(self.path, self.original)
    def test_loading_decay_and_pending_reset(self):
        records = memory.load(self.root)
        self.assertEqual([r.text for r in records], ['杉', '山'])
        self.assertEqual(records[0].weight(self.now + 30*86400), 4)
        self.assertEqual(records[0].weight(self.now - 1), 8)
        (self.root / 'learning-reset-v1').write_text('other')
        self.assertEqual(memory.load(self.root), [])
    def test_submit_queues_private_request_without_rewriting_data(self):
        record = memory.load(self.root)[0]
        request = memory.submit(self.root, settings.atomic, record, 'shan', '杉', 12)
        path = self.root / 'learning-edits' / (request + '.request')
        self.assertEqual(path.stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.path.read_text(), self.original)
        self.assertIn('shan\t杉\t12', path.read_text())
        self.assertIsNone(memory.result(self.root, request))
        settings.atomic(path.with_suffix('.result'), 'applied\n')
        self.assertEqual(memory.result(self.root, request), 'applied')
        self.assertFalse(path.with_suffix('.result').exists())
    def test_conflicts_invalid_values_and_corruption(self):
        record = memory.load(self.root)[0]
        for reading, text, count in [('shan', 'ascii', 1), ('A', '杉', 1), ('shan', '杉\n', 1), ('shan', '杉', -1), ('shan', '杉', 2**32)]:
            with self.assertRaises(ValueError): memory.submit(self.root, settings.atomic, record, reading, text, count)
        with self.assertRaises(ValueError): memory.submit(self.root, settings.atomic, record, 'shan', '山', 1)
        self.path.write_text(self.original.replace('杉\t8', '杉\t9'))
        with self.assertRaises(ValueError): memory.submit(self.root, settings.atomic, record, 'shan', '杉', 1)
        self.path.write_text('unknown')
        with self.assertRaises(ValueError): memory.load(self.root)
    def test_symlink_and_invalid_response(self):
        self.path.unlink()
        self.path.symlink_to(self.root / 'missing')
        with self.assertRaises(ValueError): memory.load(self.root)
        with self.assertRaises(ValueError): memory.result(self.root, '../invalid')
