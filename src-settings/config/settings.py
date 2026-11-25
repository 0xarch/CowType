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
DEFAULTS = dict(Version='2', ScriptMode='Off', ModeHotkey='Shift', InternalCodeHotkey='Control+Shift+G',
                VoiceHotkey='Control+Shift+V', UnicodeHotkey='Control+Shift+U',
                TranslateHotkey='Control+Shift+T', TraditionalHotkey='Control+Shift+F',
                Shuangpin='False', ShuangpinProfile='Ziranma', ShuangpinCustom='', AutoSpace='False', PageSize='5', Translate='False', UnicodePrefix='True', Punctuation='True', Sentence='True',
                Correction='True', Completion='True', Initials='True', Learning='True',
                Horizontal='False', RememberMode='False', StatisticalModel='False', SystemDictionary='False', Emoji='False', EmojiFirst='False', EmojiSkinTonePicker='True', EmojiDefaultSkinTone='0', SymbolCandidateLimit='4', EmojiCandidateLimit='4', CorrectionCandidateLimit='5', Voice='False', VoiceResident='True', VoiceModifierHold='False', ShowPinyin='True', VoiceAlwaysResident='False', PageBrackets='True', PageMinusEqual='True')
HOTKEYS = ('ModeHotkey', 'InternalCodeHotkey', 'VoiceHotkey', 'UnicodeHotkey', 'TranslateHotkey', 'TraditionalHotkey')
NAMED_KEYS = ('space', 'Tab', 'Return', 'Escape', 'BackSpace', 'Delete', 'Left', 'Right', 'Up', 'Down',
              'Home', 'End', 'Insert', 'Page_Up', 'Page_Down', 'comma', 'period', 'slash', 'semicolon',
              'equal', 'minus', 'plus', 'bracketleft', 'bracketright', 'backslash', 'grave', 'apostrophe',
              'quotedbl', 'less', 'greater', 'question', 'underscore', 'KP_Enter', 'KP_Add', 'KP_Subtract',
              'KP_Multiply', 'KP_Divide', *(f'KP_{n}' for n in range(10)))


class MigrationRequired(ValueError):
    pass


def canonical_shortcut(value):
    value = value.strip()
    if value == 'None' or value in ('Shift', 'Control', 'Alt', 'Super', 'Shift_L', 'Shift_R',
                                    'Control_L', 'Control_R', 'Alt_L', 'Alt_R', 'Super_L',
                                    'Super_R', 'ISO_Level3_Shift'):
        return value
    parts = value.split('+')
    if len(parts) == 1:
        key = parts[0]
        if (key.startswith('F') and key[1:].isdigit() and 1 <= int(key[1:]) <= 35) or key in NAMED_KEYS:
            return key
        raise ValueError(tr('error.shortcut.unsupported'))
    if not 1 < len(parts) <= 5:
        raise ValueError(tr('error.shortcut.unsupported'))
    aliases = {'Ctrl': 'Control', 'Control': 'Control', 'Alt': 'Alt', 'Shift': 'Shift',
               'Super': 'Super', 'Meta': 'Super'}
    key = parts[-1]
    if not (len(key) == 1 and key.isascii() and key.isalnum()) and not (
            key.startswith('F') and key[1:].isdigit() and 1 <= int(key[1:]) <= 35) and key not in NAMED_KEYS:
        raise ValueError(tr('error.shortcut.unsupported'))
    modifiers = []
    for part in parts[:-1]:
        if part not in aliases or aliases[part] in modifiers:
            raise ValueError(tr('error.shortcut.unsupported'))
        modifiers.append(aliases[part])
    if not modifiers and len(key) == 1 and key.isascii() and key.isalpha():
        raise ValueError(tr('error.shortcut.unsupported'))
    if len(key) == 1 and key.isascii() and key.isalpha():
        key = key.upper() if 'Shift' in modifiers else key.lower()
    return '+'.join([*(name for name in ('Control', 'Alt', 'Shift', 'Super') if name in modifiers), key])


def _shortcut_conflict(left, right):
    if left == 'None' or right == 'None':
        return False
    aliases = {'Shift': {'Shift_L', 'Shift_R'}, 'Control': {'Control_L', 'Control_R'},
               'Alt': {'Alt_L', 'Alt_R'}, 'Super': {'Super_L', 'Super_R'}}
    return left == right or right in aliases.get(left, set()) or left in aliases.get(right, set())

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
    if values.get('ScriptMode') not in ('Off', 'Strict', 'Full'):
        raise ValueError(tr('scripts.error'))
    if values.get('Version') != '2':
        raise MigrationRequired(tr('error.migration_required'))
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
    for key in HOTKEYS:
        values[key] = canonical_shortcut(values[key])
    for i, key in enumerate(HOTKEYS):
        if any(_shortcut_conflict(values[key], values[other]) for other in HOTKEYS[:i]):
            raise ValueError(tr('error.shortcut.conflict'))
    for key in DEFAULTS.keys() - {'ScriptMode', 'ShuangpinProfile', 'ShuangpinCustom', 'Version', 'PageSize', 'EmojiDefaultSkinTone', 'SymbolCandidateLimit', 'EmojiCandidateLimit', 'CorrectionCandidateLimit', *HOTKEYS}:
        if values[key] not in ('True', 'False', 'true', 'false'):
            raise ValueError(tr("message.d5553e1cedd2") + key)
    return values

def load():
    scripts = config_home() / 'scripts/records.json'
    if scripts.exists():
        if any(parent.is_symlink() for parent in (scripts, *scripts.parents)) or scripts.stat().st_size > 12_000_000:
            raise ValueError(tr('scripts.error'))
        data = json.loads(scripts.read_text(encoding='utf-8'))
        if not isinstance(data, dict) or data.get('version') != 2:
            raise MigrationRequired(tr('scripts.migration'))
    path = config_home() / 'settings-v2.ini'
    legacy = config_home() / 'settings-v1.ini'
    if not path.exists() and legacy.exists():
        raise MigrationRequired(tr('error.migration_required'))
    values = DEFAULTS.copy()
    if path.exists():
        if path.stat().st_size > 65536:
            raise ValueError(tr("message.13a0d20bc448"))
        parser = configparser.ConfigParser(interpolation=None, strict=True)
        parser.optionxform = str
        parser.read_string(path.read_text())
        if parser.sections() != ['CowType']:
            raise ValueError(tr("message.ffc05e0f887f"))
        if parser['CowType'].get('Version') != '2':
            raise MigrationRequired(tr('error.migration_required'))
        values.update(parser['CowType'])
    return validate(values)

def save(values):
    validate(values)
    atomic(config_home() / 'settings-v2.ini', '[CowType]\n' + ''.join(f'{k}={v}\n' for k, v in values.items()))

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
