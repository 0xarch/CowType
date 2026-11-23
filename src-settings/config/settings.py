# SPDX-License-Identifier: GPL-3.0-or-later
"""Versioned settings and explicit learning reset. No legacy data import."""
import configparser
import json
import os
from pathlib import Path
import tempfile
import uuid
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "i18n"))
from catalog import tr

SHUANGPIN_PROFILES = ('Ziranma', 'MS', 'Ziguang', 'ABC', 'Zhongwenzhixing', 'PinyinJiajia', 'Xiaohe', 'GB', 'Custom')
DEFAULTS = dict(Shuangpin='False', ShuangpinProfile='Ziranma', ShuangpinCustom='', AutoSpace='False', Version='1', PageSize='5', Translate='False', UnicodePrefix='True', Punctuation='True', Sentence='True',
                Correction='True', Completion='True', Initials='True', Learning='True',
                Horizontal='False', ShiftToggle='True', RememberMode='False', StatisticalModel='False', SystemDictionary='False', Emoji='False', EmojiFirst='False', EmojiSkinTonePicker='True', EmojiDefaultSkinTone='0', SymbolCandidateLimit='4', EmojiCandidateLimit='4', CorrectionCandidateLimit='5', Voice='False', VoiceResident='True', VoiceHotkey='F8', ModeHotkey='F6', InternalCodeHotkey='F7', VoiceModifierHold='False', ShowPinyin='True', VoiceAlwaysResident='False', PageBrackets='True', PageMinusEqual='True')
def shortcut_registry():
    path = Path(__file__).with_name('shortcuts.tsv')
    if not path.exists():
        path = Path(__file__).resolve().parents[2] / 'src/config/shortcuts.tsv'
    rows = [line.split('\t') for line in path.read_text().splitlines()]
    return {name: (default, tuple(keys.split(','))) for name, default, keys in rows}

SHORTCUTS = shortcut_registry()
FEATURE_HOTKEYS = SHORTCUTS['ModeHotkey'][1]
VOICE_HOTKEYS = SHORTCUTS['VoiceHotkey'][1]
for _action, (_default, _choices) in SHORTCUTS.items():
    DEFAULTS[_action] = _default

def config_home():
    return Path(os.environ.get('XDG_CONFIG_HOME') or Path.home() / '.config') / 'cowtype'

def data_home():
    return Path(os.environ.get('XDG_DATA_HOME') or Path.home() / '.local/share') / 'cowtype'

def atomic(path, text):
    path = Path(path)
    for parent in [path, *path.parents]:
        if parent.is_symlink():
            raise ValueError(tr("message.599d9125c1b0") + str(parent))
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.' + path.name, dir=path.parent)
    try:
        with os.fdopen(fd, 'w', encoding='utf-8') as out:
            out.write(text)
            out.flush()
            os.fsync(out.fileno())
        os.replace(temporary, path)
        directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)

def validate(values):
    if values.get('Version') != '1':
        raise ValueError(tr("message.de3a048a1ebc"))
    if not 3 <= int(values['PageSize']) <= 9:
        raise ValueError(tr("message.95c892b2b067"))
    if values.get('EmojiDefaultSkinTone') not in tuple(str(n) for n in range(6)):
        raise ValueError(tr('error.emoji_skin_tone'))
    for key in ('SymbolCandidateLimit', 'EmojiCandidateLimit', 'CorrectionCandidateLimit'):
        if not values[key].isdigit() or not 0 <= int(values[key]) <= 12:
            raise ValueError(tr('error.candidate_limit'))
    if values['ShuangpinProfile'] not in SHUANGPIN_PROFILES:
        raise ValueError(tr('error.shuangpin_profile'))
    path = values['ShuangpinCustom']
    if len(path) > 4096 or any(c in path for c in ('\0', '\n', '\r')) or (path and not Path(path).is_absolute()):
        raise ValueError(tr('error.shuangpin_path'))
    assigned = set()
    aliases = {'Shift': ('Shift_L', 'Shift_R'), 'Control': ('Control_L', 'Control_R'),
               'Alt': ('Alt_L', 'Alt_R'), 'Super': ('Super_L', 'Super_R'), 'None': ()}
    for name, (default, choices) in SHORTCUTS.items():
        values.setdefault(name, default)
        if values[name] not in choices:
            raise ValueError(tr('error.shortcut.unsupported'))
        physical = set(aliases.get(values[name], (values[name],)))
        if assigned & physical:
            raise ValueError(tr('error.shortcut.conflict'))
        assigned.update(physical)
    for key in DEFAULTS.keys() - {'ShuangpinProfile', 'ShuangpinCustom', 'Version', 'PageSize', 'EmojiDefaultSkinTone', 'SymbolCandidateLimit', 'EmojiCandidateLimit', 'CorrectionCandidateLimit', *SHORTCUTS}:
        if values[key] not in ('True', 'False', 'true', 'false'):
            raise ValueError(tr("message.d5553e1cedd2") + key)
    return values

def load():
    path = config_home() / 'settings-v1.ini'
    values = DEFAULTS.copy()
    if path.exists():
        if path.stat().st_size > 65536:
            raise ValueError(tr("message.13a0d20bc448"))
        parser = configparser.ConfigParser(interpolation=None, strict=True)
        parser.optionxform = str
        parser.read_string(path.read_text())
        if parser.sections() != ['CowType']:
            raise ValueError(tr("message.ffc05e0f887f"))
        if parser['CowType'].get('Version') != '1':
            raise ValueError(tr("message.de3a048a1ebc"))
        values.update(parser['CowType'])
        if 'ModeHotkey' not in parser['CowType'] and parser['CowType'].get('ShiftToggle', '').lower() == 'true':
            values['ModeHotkey'] = 'Shift'
        if 'UnicodeHotkey' not in parser['CowType'] and values['UnicodeHotkey'] in (values['ModeHotkey'], values['InternalCodeHotkey'], values['VoiceHotkey']):
            values['UnicodeHotkey'] = 'None'
        if 'TranslateHotkey' not in parser['CowType'] and values['TranslateHotkey'] in tuple(values[name] for name in SHORTCUTS if name != 'TranslateHotkey'):
            values['TranslateHotkey'] = 'None'
    return validate(values)

def save(values):
    validate(values)
    atomic(config_home() / 'settings-v1.ini', '[CowType]\n' + ''.join(f'{k}={v}\n' for k, v in values.items()))

def clear_learning():
    atomic(data_home() / 'learning-reset-v1', str(uuid.uuid4()) + '\n')

def model_dir():
    override = os.environ.get('COWTYPE_VOICE_MODEL_DIR')
    if override:
        return Path(override)
    selection = config_home() / 'voice-model.json'
    if selection.exists():
        value = json.loads(selection.read_text())
        if isinstance(value, dict) and value.get('directory'):
            return Path(value['directory'])
    return data_home() / 'paraformer-zh-streaming'

def select_model(path):
    atomic(config_home() / 'voice-model.json', json.dumps({'directory': str(Path(path).resolve())}) + '\n')
