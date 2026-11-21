"""Synthetic safety tests for source inventory tooling; no real user data."""
# SPDX-License-Identifier: GPL-3.0-or-later
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tooling'))
import development


class DevelopmentInventoryTests(unittest.TestCase):
    def test_build_and_environment_are_not_visited(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ['target', '.git', '.agents', '.aws', '.codex']:
                (root / name).mkdir()
                (root / name / 'do-not-read.bin').write_bytes(b'synthetic excluded content')
            (root / 'src').mkdir()
            (root / 'src/lib.rs').write_text('// synthetic\n')
            self.assertEqual([p.relative_to(root).as_posix() for p in development.inventory(root)], ['src/lib.rs'])

    def test_user_log_attachment_is_not_traversed_or_exported(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'cowtype-state').symlink_to('/intentionally-nonexistent-private-logs')
            self.assertEqual(development.inventory(root), [])

    def test_forbidden_root_and_model_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'core').mkdir()
            with self.assertRaisesRegex(ValueError, 'Unexpected project root'):
                development.inventory(root)
            (root / 'core').rmdir()
            (root / 'src').mkdir()
            (root / 'src/weights.onnx').write_bytes(b'synthetic not a model')
            with self.assertRaisesRegex(ValueError, 'Model/build/archive'):
                development.inventory(root)

    def test_symlink_cannot_smuggle_source_or_model(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'src').mkdir()
            (root / 'src/link.rs').symlink_to('/intentionally-nonexistent-cowtype-path')
            with self.assertRaisesRegex(ValueError, 'Symlink not allowed'):
                development.inventory(root)

    def test_source_changes_can_be_reviewed_and_snapshotted(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'src').mkdir()
            source = root / 'src/lib.rs'
            source.write_text('// first version\n')
            first = development.snapshot(development.inventory(root), root)
            source.write_text('// revised version\n')
            second = development.snapshot(development.inventory(root), root)
            self.assertNotEqual(first, second)
            self.assertEqual(second[0]['sha256'], development.sha(source))

    def test_readme_edits_keep_archive_inventory_without_content_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            readme = root / 'README.md'
            source = root / 'Cargo.toml'
            readme.write_text('# First README\n')
            source.write_text('original source')
            first = development.snapshot(development.inventory(root), root)
            readme.write_text('# Updated README with a different size\n')
            second = development.snapshot(development.inventory(root), root)
            self.assertEqual(first, second)
            self.assertIn(readme, development.inventory(root))
            self.assertEqual(next(row for row in second if row['path'] == 'README.md'),
                             {'path': 'README.md'})
            source.write_text('changed source')
            self.assertNotEqual(second, development.snapshot(development.inventory(root), root))
            readme.write_text('[broken](missing.md)\n')
            with self.assertRaisesRegex(ValueError, 'Broken/escaping Markdown link'):
                development.verify_links(development.inventory(root), root)

    def test_new_component_directory_is_supported(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'src-example').mkdir()
            (root / 'src-example/main.rs').write_text('// component\n')
            self.assertEqual(len(development.inventory(root)), 1)


if __name__ == '__main__':
    unittest.main()
