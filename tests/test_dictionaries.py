# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent synthetic parser/installation tests; no user XDG access or network."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import threading
import unittest

spec = importlib.util.spec_from_file_location('dictionary_manager', Path(__file__).resolve().parents[1] / 'src-data-tools/dictionaries/manage.py')
manager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manager)


class DictionaryTests(unittest.TestCase):
    def test_pinned_profiles_no_models_and_no_implicit_wanxiang(self):
        profiles = manager.registry()
        self.assertEqual({k for k, p in profiles.items() if p['default']}, {'ice', 'wubi'})
        for profile in profiles.values():
            self.assertRegex(profile['revision'], r'^[0-9a-f]{40}$')
            for record in profile['files']:
                self.assertRegex(record['sha256'], r'^[0-9a-f]{64}$')
                self.assertIn(record['url'].split('/')[-1].split('.')[-1], {'yaml', 'md', 'LICENSE', 'AUTHORS'})

    def test_active_import_graph_only_and_unsafe_path_rejected(self):
        data = b'---\nimport_tables:\n  - cn/a # active\n  # - cn/ignored\nsort: by_weight\n...\n'
        self.assertEqual(manager.imports(data), ['cn/a.dict.yaml'])
        with self.assertRaises(ValueError):
            manager.imports(b'---\nimport_tables:\n  - ../escape\n...\n')

    def test_tone_normalization_and_diaeresis_do_not_merge_nv_with_nu(self):
        self.assertEqual(manager.normalize_reading('nǚ lǜ nüè jǔ qǔ xǔ yǔ'), 'nv lv nve ju qu xu yu')
        self.assertEqual(manager.normalize_reading('nù'), 'nu')
        self.assertIsNone(manager.normalize_reading('nǐ/ni'))

    def test_tsv_reordering_zero_weight_and_skips_are_counted(self):
        data = '---\nname: synthetic\n...\n山\tshān\t0\n海\thǎi\n坏\tBAD!\t1\n空\n'.encode()
        output = io.StringIO()
        report = manager.convert_table(data, 'pinyin-toned', output)
        self.assertEqual(output.getvalue(), 'shan\t山\t1\nhai\t海\t1\n')
        self.assertEqual(report, {'accepted': 2, 'skipped': 2, 'zero_weights_floored': 1})

    def test_wubi_stems_ignored_zero_retained_overflow_skipped(self):
        data = '---\n...\n工\ta\t0\taa\n错\tz\t4\n错\taa\t18446744073709551616\n'.encode()
        output = io.StringIO()
        report = manager.convert_table(data, 'wubi86', output)
        self.assertEqual(output.getvalue(), 'a\t工\t0\n')
        self.assertEqual(report['skipped'], 2)

    def test_digest_mismatch_fails_before_conversion(self):
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            manager.check_bytes(b'new', {'bytes': 3, 'sha256': hashlib.sha256(b'old').hexdigest(), 'path': 'example'})

    def test_unmanaged_optional_directory_is_never_removed(self):
        with tempfile.TemporaryDirectory() as temp:
            home = Path(temp)
            path = manager.managed_path(home)
            path.mkdir(parents=True)
            personal = path / 'my-words.tsv'
            personal.write_text('synthetic personal file')
            with self.assertRaises((OSError, ValueError)):
                manager.remove_optional(home)
            self.assertTrue(personal.exists())

    def test_remove_only_managed_optional_not_personal_or_system_data(self):
        with tempfile.TemporaryDirectory() as temp:
            home = Path(temp)
            path = manager.managed_path(home)
            path.mkdir(parents=True)
            (path / 'manifest.json').write_text(json.dumps({'managed_by': manager.OWNER, 'profile': 'wanxiang'}))
            personal = home / 'cowtype/user.tsv'
            personal.write_text('synthetic user data')
            self.assertTrue(manager.remove_optional(home))
            self.assertTrue(personal.exists())
            self.assertFalse(manager.remove_optional(home))

    def test_symlink_optional_directory_cannot_delete_target(self):
        with tempfile.TemporaryDirectory() as temp:
            home = Path(temp)
            path = manager.managed_path(home)
            path.parent.mkdir(parents=True)
            victim = home / 'unrelated'
            victim.mkdir()
            path.symlink_to(victim)
            with self.assertRaises(ValueError):
                manager.remove_optional(home)
            self.assertTrue(victim.exists())

    def test_personal_file_inside_managed_directory_blocks_removal(self):
        with tempfile.TemporaryDirectory() as temp:
            home = Path(temp)
            path = manager.managed_path(home)
            path.mkdir(parents=True)
            (path / 'manifest.json').write_text(json.dumps({'managed_by': manager.OWNER, 'profile': 'wanxiang'}))
            personal = path / 'my-words.tsv'
            personal.write_text('synthetic personal file')
            with self.assertRaisesRegex(ValueError, 'unrelated files'):
                manager.remove_optional(home)
            self.assertTrue(personal.exists())

    def test_prepare_preserves_license_sources_and_reproducible_output(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            cache = root / 'cache'
            cache.mkdir()
            files = {'main.dict.yaml': b'---\nimport_tables:\n  - words\n...\n',
                     'words.dict.yaml': '---\n...\n山\tshan\t2\n'.encode(), 'LICENSE': b'Synthetic test license, not real upstream data'}
            profile = {'files': [], 'root': 'main.dict.yaml', 'tables': ['words.dict.yaml'],
                       'excluded_imports': {}, 'scheme': 'pinyin', 'repository': 'synthetic/test', 'revision': '0' * 40}
            for path, data in files.items():
                (cache / path).write_bytes(data)
                profile['files'].append({'path': path, 'bytes': len(data), 'sha256': manager.digest(data)})
            a = manager.prepare('synthetic', profile, cache, root / 'a')
            b = manager.prepare('synthetic', profile, cache, root / 'b')
            self.assertEqual(a['output'], b['output'])
            self.assertEqual((root / 'a/source/LICENSE').read_bytes(), files['LICENSE'])
            self.assertEqual((root / 'a/source/words.dict.yaml').read_bytes(), files['words.dict.yaml'])


if __name__ == '__main__':
    unittest.main()
