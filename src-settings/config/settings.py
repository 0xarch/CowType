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

DEFAULTS = dict(Version='1', PageSize='5', Punctuation='True', Sentence='True',
                Correction='True', Completion='True', Initials='True', Learning='True',
                Horizontal='False', ShiftToggle='True', RememberMode='False', Voice='False', VoiceResident='True', VoiceHotkey='F8', VoiceModifierHold='False', ShowPinyin='True', VoiceAlwaysResident='False', PageBrackets='True', PageMinusEqual='True')
VOICE_HOTKEYS = ('F8', 'F9', 'F10', 'Alt_L', 'Alt_R', 'ISO_Level3_Shift', 'Control_R')

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
    if 'VoiceHotkey' not in values:
        values['VoiceHotkey'] = DEFAULTS['VoiceHotkey']
    if values.get('VoiceHotkey') not in VOICE_HOTKEYS:
        raise ValueError('Unsupported voice shortcut')
    for key in DEFAULTS.keys() - {'Version', 'PageSize', 'VoiceHotkey'}:
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
