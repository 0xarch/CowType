# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('settings', Path(__file__).resolve().parents[1] / 'src-settings/config/settings.py')
settings = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(settings)

class SettingsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.env = patch.dict(os.environ, XDG_CONFIG_HOME=str(self.root/'config'), XDG_DATA_HOME=str(self.root/'data'), COWTYPE_VOICE_MODEL_DIR='')
        self.env.start()
    def tearDown(self):
        self.env.stop()
        self.temporary.cleanup()
    def test_shortcut_conflicts_and_disabled_bindings(self):
        for changes in [dict(ModeHotkey='F7'), dict(InternalCodeHotkey='F8'), dict(VoiceHotkey='F6'), dict(InternalCodeHotkey='F13')]:
            with self.assertRaises(ValueError):
                settings.validate(dict(settings.DEFAULTS, **changes))
        settings.validate(dict(settings.DEFAULTS, ModeHotkey='None', InternalCodeHotkey='None'))
        settings.validate(dict(settings.DEFAULTS, ModeHotkey='F11', InternalCodeHotkey='F12'))
    def test_common_shortcuts_and_legacy_shift_migration(self):
        self.assertEqual(len({choices for _, choices in settings.SHORTCUTS.values()}), 1)
        for role in settings.SHORTCUTS:
            for key in ('Shift', 'Alt', 'Control', 'Super', 'Shift_R', 'Control_L', 'None'):
                settings.validate(dict(settings.DEFAULTS, **{role: key}))
        for generic, side in [('Shift', 'Shift_L'), ('Alt', 'Alt_R'), ('Control', 'Control_R')]:
            with self.assertRaises(ValueError):
                settings.validate(dict(settings.DEFAULTS, ModeHotkey=generic, VoiceHotkey=side))
        settings.atomic(settings.config_home()/'settings-v1.ini', '[CowType]\nVersion=1\nShiftToggle=True\n')
        self.assertEqual(settings.load()['ModeHotkey'], 'Shift')
        settings.atomic(settings.config_home()/'settings-v1.ini', '[CowType]\nVersion=1\nShiftToggle=True\nModeHotkey=F6\n')
        self.assertEqual(settings.load()['ModeHotkey'], 'F6')
    def test_emoji_defaults_off_and_roundtrips(self):
        values = settings.load()
        self.assertEqual(values['Emoji'], 'False')
        values['Emoji'] = 'True'
        settings.save(values)
        self.assertEqual(settings.load()['Emoji'], 'True')
    def test_roundtrip_unknown_key_private_permissions(self):
        values = settings.load()
        values.update(PageSize='9', Sentence='False', FutureOption='preserved')
        settings.save(values)
        self.assertEqual(settings.load(), values)
        self.assertEqual((settings.config_home()/'settings-v1.ini').stat().st_mode & 0o777, 0o600)
    def test_invalid_config_never_overwritten(self):
        path = settings.config_home()/'settings-v1.ini'
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
        path = settings.config_home()/'settings-v1.ini'
        path.parent.mkdir(parents=True)
        path.symlink_to(target)
        with self.assertRaises(ValueError): settings.save(settings.DEFAULTS)
        self.assertEqual(target.read_text(), 'keep')
    def test_remember_mode_defaults_and_roundtrip(self):
        settings.atomic(settings.config_home()/'settings-v1.ini', '[CowType]\nVersion=1\n')
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
