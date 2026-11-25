# SPDX-License-Identifier: GPL-3.0-or-later
"""Script persistence uses disposable XDG directories and never executes Lua."""
import importlib.util
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
def module(name, file):
    spec = importlib.util.spec_from_file_location(name, ROOT / file)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value
settings = module('script_settings', 'src-settings/config/settings.py')
scripts = module('script_store', 'src-settings/config/scripts.py')

class ScriptsTests(unittest.TestCase):
    def test_modes_and_enabled_index(self):
        with tempfile.TemporaryDirectory(prefix='cowtype-script-test-') as tmp:
            with patch.dict(os.environ, XDG_CONFIG_HOME=tmp):
                home = settings.config_home()
                self.assertEqual(settings.load()['ScriptMode'], 'Off')
                for mode in ('Off', 'Strict', 'Full'):
                    settings.save(dict(settings.DEFAULTS, ScriptMode=mode))
                    self.assertEqual(settings.load()['ScriptMode'], mode)
                with self.assertRaises(ValueError):
                    settings.save(dict(settings.DEFAULTS, ScriptMode='anything'))
                record = scripts.new_record('Date', 'riqi', scripts.example('date'))
                self.assertFalse(record['enabled'])
                scripts.save(home, settings.atomic, [record])
                self.assertEqual(scripts.load(home), [record])
                self.assertEqual((home/'scripts/index.tsv').read_text(), scripts.HEADER)
                record['enabled'] = True
                scripts.save(home, settings.atomic, [record])
                filename, trigger, independent = (home/'scripts/index.tsv').read_text().splitlines()[1].split('\t')
                self.assertEqual(independent, '0')
                self.assertEqual(trigger, 'riqi')
                self.assertEqual((home/'scripts'/(filename+'.lua')).read_text(), record['source'])
                old = home/'scripts'/(filename+'.lua')
                record['source'] = 'return {5, "edited"}'
                scripts.save(home, settings.atomic, [record])
                self.assertFalse(old.exists())
                record['enabled'] = False
                scripts.save(home, settings.atomic, [record])
                self.assertEqual((home/'scripts/index.tsv').read_text(), scripts.HEADER)
                self.assertFalse(list((home/'scripts').glob('*.lua')))
                scripts.save(home, settings.atomic, [])
                self.assertEqual(scripts.load(home), [])

    def test_bad_records_and_symlinked_parent_rejected(self):
        good = scripts.new_record('Example', 'test')
        for fields in (dict(trigger='../outside'), dict(name=''), dict(source='x'*65537), dict(enabled='True'), dict(id='../escape')):
            with self.assertRaises(ValueError):
                scripts.validate([dict(good, **fields)])
        with self.assertRaises(ValueError): scripts.validate([good, good])
        with tempfile.TemporaryDirectory(prefix='cowtype-script-test-') as tmp:
            root = Path(tmp)
            target = root/'outside'; target.mkdir()
            home = root/'config/cowtype'; home.parent.mkdir(parents=True)
            home.symlink_to(target, target_is_directory=True)
            with self.assertRaises(ValueError): scripts.save(home, settings.atomic, [good])
            self.assertEqual(list(target.iterdir()), [])
            home.unlink(); home.mkdir()
            (home/'scripts').symlink_to(target, target_is_directory=True)
            with self.assertRaises(ValueError): scripts.load(home)
            with self.assertRaises(ValueError): scripts.save(home, settings.atomic, [])

    def test_examples_are_text_scripts(self):
        self.assertIn("os.date('%Y-%m-%d')", scripts.example('date'))
        self.assertIn("os.date('%H:%M')", scripts.example('time'))
        self.assertEqual(scripts.EXAMPLES['date'], ('riqi', False))
        self.assertEqual(scripts.EXAMPLES['time'], ('shijian', False))
        self.assertEqual(scripts.EXAMPLES['system'], ('xitong', True))
        self.assertIn("io.popen('uname '", scripts.example('system'))

    def test_ranked_return_validation_without_execution(self):
        valid = ["return {5, input}", "return {{5, os.date('%H:%M')}, {6, 'time'}}",
                 "local function text() return 'value' end; return {5, text()}",
                 "if input == 'x' then return {{-1, 'a'}} end; return {}",
                 "-- return 'not code'\nreturn {5.5, [=[return 'text']=]}"]
        for source in valid: scripts.validate_return(source, False)
        for source in ("return input", "return 'text'", "return {'a', 'b'}", "return {5, 'a', 'b'}",
                       "return {{5, 'a'}, 'b'}", "return {5, 'a'} or 'b'", "-- return {5, 'fake'}",
                       "local function x() return {5,'x'} end; return x()"):
            with self.subTest(source=source), self.assertRaises(ValueError):
                scripts.validate_return(source, False)
        scripts.validate_return("os.execute('never execute this'); return 'text'", True)

    def test_legacy_scripts_need_explicit_migration(self):
        import json
        import importlib.util
        import sys
        with tempfile.TemporaryDirectory(prefix='cowtype-script-migration-') as tmp:
            with patch.dict(os.environ, XDG_CONFIG_HOME=tmp):
                home = settings.config_home(); directory = home/'scripts'; directory.mkdir(parents=True)
                record = scripts.new_record('Legacy', 'riqi', "return {os.date('%Y')}" )
                del record['independent']
                original = json.dumps([record])
                (directory/'records.json').write_text(original)
                (directory/'index.tsv').write_text('old\triqi\n')
                with self.assertRaises(settings.MigrationRequired): settings.load()
                with patch.dict(sys.modules, settings=settings):
                    migrator = module('scripts_migrator', 'src-settings/config/migrate.py')
                migrator.migrate()
                self.assertEqual(scripts.load(home)[0]['independent'], True)
                self.assertEqual(scripts.load(home)[0]['source'], record['source'])
                self.assertEqual((next(home.glob('scripts.backup-*'))/'records.json').read_text(), original)
                self.assertEqual(settings.load()['ScriptMode'], 'Off')

    def test_failed_script_migration_restores_entire_directory(self):
        import json
        import sys
        with tempfile.TemporaryDirectory(prefix='cowtype-script-migration-') as tmp:
            with patch.dict(os.environ, XDG_CONFIG_HOME=tmp):
                home = settings.config_home(); directory = home/'scripts'; directory.mkdir(parents=True)
                record = scripts.new_record('Legacy', 'riqi', "return 'value'")
                del record['independent']
                (directory/'records.json').write_text(json.dumps([record]))
                (directory/'index.tsv').write_text('legacy\triqi\n')
                (directory/'legacy.lua').write_text(record['source'])
                original = {p.name: p.read_bytes() for p in directory.iterdir()}
                with patch.dict(sys.modules, settings=settings):
                    migrator = module('scripts_migration_failure', 'src-settings/config/migrate.py')
                with patch.object(settings, 'atomic', side_effect=OSError('synthetic write failure')):
                    with self.assertRaises(OSError): migrator.migrate()
                self.assertEqual({p.name: p.read_bytes() for p in directory.iterdir()}, original)

    def test_permission_hints_ignore_strings_comments_and_include_alias_sources(self):
        self.assertEqual(scripts.inspect(scripts.example('date'), False), [])
        self.assertEqual(scripts.inspect(scripts.example('time'), False), [])
        self.assertIn(('io', 6), scripts.inspect(scripts.example('system'), True))
        self.assertEqual(scripts.inspect("-- io.popen('ignored')\nreturn {5, 'os.execute io require'}", False), [])
        self.assertEqual(scripts.inspect('local run = os.execute\nreturn {5, input}', False), [('os.execute', 1)])
        self.assertEqual(scripts.inspect('local system = os\nreturn {5, input}', False), [('os (dynamic access)', 1)])
        self.assertEqual(scripts.inspect('return {{5, input}}', False), [])
        with self.assertRaises(scripts.CheckError) as failure:
            scripts.inspect("-- comment\nreturn 'plain'", False)
        self.assertEqual(failure.exception.line, 2)
        with self.assertRaises(scripts.CheckError) as failure:
            scripts.inspect('--[=[ unclosed', True)
        self.assertEqual(failure.exception.key, 'scripts.check.unclosed')
