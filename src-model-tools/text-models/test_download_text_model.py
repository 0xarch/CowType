#!/usr/bin/env python3
"""Independent tests for the newly authored text-model preparation tool; no network."""
import hashlib
import importlib.util
import io
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('text_model', Path(__file__).with_name('download_text_model.py'))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

class ModelPreparationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='cowtype-free-model-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.payloads = {'LICENSE': b'synthetic test license', 'fixture.gguf': b'synthetic non-model bytes'}
        self.model = {'repository': 'Example/Fixture', 'source': 'modelscope',
                      'revision': 'a' * 40, 'license': 'Apache-2.0',
                      'files': [{'path': k, 'size': len(v), 'sha256': hashlib.sha256(v).hexdigest()}
                                for k, v in self.payloads.items()]}
        self.output = self.root / 'selected-model'

    def fetch(self, model, item, target, timeout):
        target.write_bytes(self.payloads[item['path']])

    def test_offline_preflight_does_not_download_or_create_output(self):
        with patch('sys.argv', ['tool', '--check', '--output', str(self.output)]), \
             patch.object(m, 'fetch', side_effect=AssertionError('network call')), \
             patch('sys.stdout', io.StringIO()):
            self.assertEqual(m.main(), 0)
        self.assertFalse(self.output.exists())

    def test_rejects_download_without_explicit_license_choice(self):
        with patch('sys.argv', ['tool', '--output', str(self.output)]), \
             patch('sys.stdout', io.StringIO()), patch('sys.stderr', io.StringIO()), \
             self.assertRaises(SystemExit):
            m.main()
        self.assertFalse(self.output.exists())

    def test_transactional_download_and_existing_mismatch_protection(self):
        calls = []
        def fetch(model, item, target, timeout):
            calls.append(item['path'])
            self.fetch(model, item, target, timeout)
        with patch.object(m, 'fetch', side_effect=fetch):
            m.prepare(self.model, self.output, 1)
            self.assertEqual(calls[0], 'LICENSE')
            self.assertEqual(json.loads((self.output / 'SOURCE.json').read_text())['revision'], 'a' * 40)
            m.prepare(self.model, self.output, 1)
            self.assertEqual(len(calls), 2)
            (self.output / 'fixture.gguf').write_bytes(b'changed')
            with self.assertRaises(ValueError):
                m.prepare(self.model, self.output, 1)
            self.assertEqual((self.output / 'fixture.gguf').read_bytes(), b'changed')

    def test_failed_download_does_not_publish_partial_output(self):
        def broken(model, item, target, timeout):
            target.write_bytes(b'partial')
            raise OSError('network failed')
        with patch.object(m, 'fetch', side_effect=broken), self.assertRaises(OSError):
            m.prepare(self.model, self.output, 1)
        self.assertFalse(self.output.exists())
        self.assertEqual(list(self.root.glob('.cowtype-text-model-*')), [])

    def test_stream_rejects_hash_and_length_mismatch(self):
        class Response(io.BytesIO):
            def geturl(self):
                return 'https://example.invalid/model'
        item = self.model['files'][0]
        for content in (b'too short', self.payloads['LICENSE'] + b'extra', b'X' * item['size']):
            target = self.root / 'bad'
            with patch.object(m.urllib.request, 'urlopen', return_value=Response(content)), \
                 self.assertRaises(ValueError):
                m.fetch(self.model, item, target, 1)
            target.unlink(missing_ok=True)

    def test_catalog_blocks_path_escape_and_non_fixed_revision(self):
        catalog = self.root / 'catalog.json'
        with patch.object(m, 'CATALOG', catalog):
            for field, value in [('path', '../secret'), ('sha256', 'not-a-hash'), ('size', -1)]:
                bad = json.loads(json.dumps(self.model))
                bad['files'][0][field] = value
                catalog.write_text(json.dumps({'format': 1, 'models': {'fixture': bad}}))
                with self.subTest(field=field), self.assertRaises(ValueError):
                    m.load_model('fixture')
            self.model['revision'] = 'main'
            catalog.write_text(json.dumps({'format': 1, 'models': {'fixture': self.model}}))
            with self.assertRaises(ValueError):
                m.load_model('fixture')

    def test_selection_is_private_disabled_and_never_overwrites(self):
        with patch.dict(os.environ, {'XDG_CONFIG_HOME': str(self.root / 'config')}):
            selection = m.select_model(self.model, self.output)
            self.assertFalse(json.loads(selection.read_text())['enabled'])
            self.assertEqual(selection.stat().st_mode & 0o777, 0o600)
            before = selection.read_bytes()
            with self.assertRaises(ValueError):
                m.select_model(self.model, self.output)
            self.assertEqual(selection.read_bytes(), before)

    def test_symlink_output_rejected(self):
        other = self.root / 'other'
        other.mkdir()
        self.output.symlink_to(other)
        with self.assertRaises(ValueError):
            m.prepare(self.model, self.output, 1)

    def test_selection_rejects_symlinked_config_components(self):
        for linked in ('cowtype', 'config', 'ancestor'):
            with self.subTest(linked=linked), tempfile.TemporaryDirectory(dir=self.root) as temp:
                base = Path(temp)
                outside = base / 'outside'
                outside.mkdir()
                if linked == 'cowtype':
                    config = base / 'config'
                    config.mkdir()
                    (config / 'cowtype').symlink_to(outside, target_is_directory=True)
                elif linked == 'config':
                    config = base / 'config'
                    config.symlink_to(outside, target_is_directory=True)
                else:
                    (base / 'ancestor').symlink_to(outside, target_is_directory=True)
                    config = base / 'ancestor/config'
                with patch.dict(os.environ, XDG_CONFIG_HOME=str(config)), self.assertRaisesRegex(ValueError, 'symlinks'):
                    m.select_model(self.model, self.output)
                self.assertEqual(list(outside.iterdir()), [])

if __name__ == '__main__':
    unittest.main()
