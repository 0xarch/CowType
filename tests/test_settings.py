# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('settings', Path(__file__).resolve().parents[1] / 'src-settings/config/settings.py')
settings = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(settings)
sys.modules['settings'] = settings
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'src-settings/config'))
MIGRATOR_SPEC = importlib.util.spec_from_file_location('migrate', Path(__file__).resolve().parents[1] / 'src-settings/config/migrate.py')
migrator = importlib.util.module_from_spec(MIGRATOR_SPEC)
MIGRATOR_SPEC.loader.exec_module(migrator)

class SettingsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.env = patch.dict(os.environ, XDG_CONFIG_HOME=str(self.root/'config'), XDG_DATA_HOME=str(self.root/'data'), COWTYPE_VOICE_MODEL_DIR='')
        self.env.start()
    def tearDown(self):
        self.env.stop()
        self.temporary.cleanup()
    def test_shuangpin_and_autospace_configuration(self):
        values = settings.load()
        self.assertEqual(values['Shuangpin'], 'False')
        self.assertEqual(values['AutoSpace'], 'False')
        self.assertEqual(values['ShuangpinProfile'], 'Ziranma')
        for profile in settings.SHUANGPIN_PROFILES:
            values.update(Shuangpin='True', ShuangpinProfile=profile, ShuangpinCustom=str(self.root / 'scheme.conf'), AutoSpace='True')
            settings.save(values)
            self.assertEqual(settings.load(), values)
        for change in (dict(ShuangpinProfile='invalid'), dict(ShuangpinCustom='relative.conf'), dict(AutoSpace='maybe')):
            with self.assertRaises(ValueError):
                settings.validate(dict(values, **change))
    def test_shortcut_conflicts_and_disabled_bindings(self):
        for changes in [dict(ModeHotkey='Control+Shift+G'), dict(InternalCodeHotkey='Control+Shift+V'),
                        dict(VoiceHotkey='F36'), dict(InternalCodeHotkey='F36')]:
            with self.assertRaises(ValueError):
                settings.validate(dict(settings.DEFAULTS, **changes))
        settings.validate(dict(settings.DEFAULTS, ModeHotkey='None', InternalCodeHotkey='None'))
        settings.validate(dict(settings.DEFAULTS, ModeHotkey='F11', InternalCodeHotkey='F12'))
        self.assertEqual(settings.canonical_shortcut('Control+Shift+g'), 'Control+Shift+G')
        self.assertEqual(settings.canonical_shortcut('Control+comma'), 'Control+comma')
        with self.assertRaises(ValueError):
            settings.validate(dict(settings.DEFAULTS, TraditionalHotkey='Control+Shift+g'))
    def test_common_shortcuts_and_explicit_migration_boundary(self):
        for role in settings.HOTKEYS:
            for key in ('Shift', 'Alt', 'Control', 'Super', 'Shift_R', 'Control_L', 'None', 'Control+Shift+F'):
                values = dict(settings.DEFAULTS, **{name: 'None' for name in settings.HOTKEYS})
                values[role] = key
                if key == 'Control+Shift+F' and role != 'TraditionalHotkey':
                    values['TraditionalHotkey'] = 'Control+Alt+F'
                settings.validate(values)
        for generic, side in [('Shift', 'Shift_L'), ('Alt', 'Alt_R'), ('Control', 'Control_R')]:
            with self.assertRaises(ValueError):
                settings.validate(dict(settings.DEFAULTS, ModeHotkey=generic, VoiceHotkey=side))
        settings.atomic(settings.config_home()/'settings-v1.ini', '[CowType]\nVersion=1\n')
        with self.assertRaises(settings.MigrationRequired):
            settings.load()
    def test_migrator_backs_up_and_writes_new_format(self):
        directory = settings.config_home()
        directory.mkdir(parents=True)
        source = directory/'settings-v1.ini'
        original = '[CowType]\nVersion=1\nModeHotkey=F6\nVoiceHotkey=F8\nLearning=False\n'
        source.write_text(original)
        migrator.migrate()
        destination = directory/'settings-v2.ini'
        self.assertTrue(destination.exists())
        self.assertFalse(source.exists())
        backups = list(directory.glob('settings-v1.ini.backup-*'))
        self.assertEqual(len(backups), 1)
        self.assertEqual(backups[0].read_text(), original)
        converted = settings.load()
        self.assertEqual(converted['Version'], '2')
        self.assertEqual(converted['ModeHotkey'], 'Shift')
        self.assertEqual(converted['VoiceHotkey'], 'Control+Shift+V')
        self.assertEqual(converted['Learning'], 'False')
    def test_migrator_refuses_bad_or_conflicting_input_without_changes(self):
        directory = settings.config_home()
        directory.mkdir(parents=True)
        source = directory/'settings-v1.ini'
        original = '[CowType]\nVersion=1\nModeHotkey=F6\nVoiceHotkey=Shift\n'
        source.write_text(original)
        with self.assertRaises(ValueError):
            migrator.migrate()
        self.assertEqual(source.read_text(), original)
        self.assertFalse((directory/'settings-v2.ini').exists())
        settings.atomic(settings.config_home()/'settings-v2.ini', '[CowType]\nVersion=1\nShiftToggle=True\n')
        with self.assertRaises(settings.MigrationRequired):
            settings.load()
    def test_unicode_binding_is_independent_from_legacy_config(self):
        self.assertEqual(settings.DEFAULTS['UnicodeHotkey'], 'Control+Shift+U')
        settings.validate(dict(settings.DEFAULTS, UnicodeHotkey='None'))
        settings.validate(dict(settings.DEFAULTS, UnicodeHotkey='Control_R', InternalCodeHotkey='Alt_L'))
    def test_unicode_prefix_defaults_on_and_is_independent(self):
        values = settings.load()
        self.assertEqual(values['UnicodePrefix'], 'True')
        values.update(UnicodeHotkey='None')
        settings.save(values)
        self.assertEqual(settings.load()['UnicodePrefix'], 'True')
        values.update(UnicodePrefix='False', UnicodeHotkey='Control+Alt+U')
        settings.save(values)
        self.assertEqual(settings.load()['UnicodePrefix'], 'False')
        self.assertEqual(settings.load()['UnicodeHotkey'], 'Control+Alt+u')
    def test_translation_defaults_and_conflict_validation(self):
        self.assertEqual(settings.DEFAULTS['Translate'], 'False')
        self.assertEqual(settings.DEFAULTS['TranslateHotkey'], 'Control+Shift+T')
        values=dict(settings.DEFAULTS, Translate='True', TranslateHotkey='Alt')
        settings.save(values)
        self.assertEqual(settings.load(),values)
        with self.assertRaises(ValueError):
            settings.validate(dict(values, ModeHotkey='Alt'))
    def test_system_dictionary_is_optional_and_independent(self):
        self.assertEqual(settings.load()['SystemDictionary'], 'False')
        for enabled in ('True', 'False'):
            values = dict(settings.load(), SystemDictionary=enabled, StatisticalModel='True')
            settings.save(values)
            self.assertEqual(settings.load(), values)
        with self.assertRaises(ValueError):
            settings.validate(dict(settings.DEFAULTS, SystemDictionary='auto'))

    def test_emoji_defaults_off_and_roundtrips(self):
        values = settings.load()
        self.assertEqual(values['Emoji'], 'False')
        values['Emoji'] = 'True'
        settings.save(values)
        self.assertEqual(settings.load()['Emoji'], 'True')
    def test_emoji_priority_defaults_to_symbols_and_roundtrips(self):
        values = settings.load()
        self.assertEqual(values['EmojiFirst'], 'False')
        for enabled in ('True', 'False'):
            values.update(Emoji='True', EmojiFirst=enabled)
            settings.save(values)
            self.assertEqual(settings.load()['EmojiFirst'], enabled)
        with self.assertRaises(ValueError):
            settings.validate(dict(values, EmojiFirst='auto'))

    def test_emoji_skin_tone_choices_validate_and_roundtrip(self):
        values = settings.load()
        self.assertEqual(values['EmojiSkinTonePicker'], 'True')
        self.assertEqual(values['EmojiDefaultSkinTone'], '0')
        for tone in range(6):
            values.update(Emoji='True', EmojiSkinTonePicker='False', EmojiDefaultSkinTone=str(tone))
            settings.save(values)
            self.assertEqual(settings.load(), values)
        for invalid in ('6', '-1', 'dark', '1.0'):
            with self.assertRaises(ValueError):
                settings.validate(dict(values, EmojiDefaultSkinTone=invalid))

    def test_candidate_limits_defaults_validation_and_roundtrip(self):
        values = settings.load()
        self.assertEqual((values['SymbolCandidateLimit'], values['EmojiCandidateLimit'], values['CorrectionCandidateLimit']), ('4', '4', '5'))
        for key in ('SymbolCandidateLimit', 'EmojiCandidateLimit', 'CorrectionCandidateLimit'):
            values[key] = '0'
            settings.save(values)
            self.assertEqual(settings.load()[key], '0')
            for invalid in ('13', '-1', 'many'):
                with self.assertRaises(ValueError):
                    settings.validate(dict(values, **{key: invalid}))

    def test_roundtrip_unknown_key_private_permissions(self):
        values = settings.load()
        values.update(PageSize='9', Sentence='False', FutureOption='preserved')
        settings.save(values)
        self.assertEqual(settings.load(), values)
        self.assertEqual((settings.config_home()/'settings-v2.ini').stat().st_mode & 0o777, 0o600)
    def test_invalid_config_never_overwritten(self):
        path = settings.config_home()/'settings-v2.ini'
        settings.atomic(path, '[CowType]\nVersion=99\n')
        with self.assertRaises(ValueError): settings.load()
        self.assertIn('99', path.read_text())
        with self.assertRaises(ValueError): settings.save(dict(settings.DEFAULTS, PageSize='10'))
    def test_reset_marker_leaves_dictionary_and_history_for_engine(self):
        history = settings.data_home()/'learning-v1.tsv'
        settings.atomic(history, 'original')
        settings.clear_learning()
        first = (settings.data_home()/'learning-reset-v1').read_text()
        settings.clear_learning()
        self.assertNotEqual(first, (settings.data_home()/'learning-reset-v1').read_text())
        self.assertEqual(history.read_text(), 'original')
    def test_original_model_selection_contract(self):
        directory = self.root/'exported'
        settings.select_model(directory)
        self.assertEqual(settings.model_dir(), directory)
        selection = json.loads((settings.config_home()/'voice-model.json').read_text())
        self.assertEqual(selection['directory'], str(directory))
        with patch.dict(os.environ, COWTYPE_VOICE_MODEL_DIR=str(self.root/'override')):
            self.assertEqual(settings.model_dir(), self.root/'override')
    def test_symlink_rejected(self):
        target = self.root/'private'
        target.write_text('keep')
        path = settings.config_home()/'settings-v2.ini'
        path.parent.mkdir(parents=True)
        path.symlink_to(target)
        with self.assertRaises(ValueError): settings.save(settings.DEFAULTS)
        self.assertEqual(target.read_text(), 'keep')
    def test_remember_mode_defaults_and_roundtrip(self):
        settings.atomic(settings.config_home()/'settings-v2.ini', '[CowType]\nVersion=2\n')
        values = settings.load()
        self.assertEqual(values['RememberMode'], 'False')
        values.update(RememberMode='True', ShiftToggle='False')
        settings.save(values)
        self.assertEqual(settings.load(), values)

    def test_voice_hold_and_candidate_reading_options(self):
        values = settings.load()
        self.assertEqual(values['VoiceModifierHold'], 'False')
        self.assertEqual(values['ShowPinyin'], 'True')
        values.update(VoiceModifierHold='True', ShowPinyin='False')
        settings.save(values)
        self.assertEqual(settings.load(), values)
