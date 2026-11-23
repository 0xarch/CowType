#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Explicitly migrate CowType settings v1 to v2."""
from datetime import datetime, timezone
import configparser
import os
from pathlib import Path
import subprocess
import tempfile
import sys

import settings


OLD_HOTKEY_DEFAULTS = {
    'ModeHotkey': 'F6',
    'InternalCodeHotkey': 'F7',
    'VoiceHotkey': 'F8',
    'UnicodeHotkey': 'F9',
    'TranslateHotkey': 'F10',
}
REPLACEMENTS = {
    'ModeHotkey': 'Shift',
    'InternalCodeHotkey': 'Control+Shift+G',
    'VoiceHotkey': 'Control+Shift+V',
    'UnicodeHotkey': 'Control+Shift+U',
    'TranslateHotkey': 'Control+Shift+T',
}


def _check_paths(source, destination):
    for path in (source, destination):
        if path.is_symlink():
            raise ValueError(f'Refusing symbolic-link settings file: {path.name}')
    for parent in (source.parent, *source.parent.parents):
        if parent.is_symlink():
            raise ValueError(f'Refusing symbolic-link configuration directory: {parent}')


def _backup_path(source):
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    candidate = source.with_name(f'{source.name}.backup-{stamp}')
    suffix = 1
    while candidate.exists():
        candidate = source.with_name(f'{source.name}.backup-{stamp}-{suffix}')
        suffix += 1
    return candidate


def migrate():
    directory = settings.config_home()
    source = directory / 'settings-v1.ini'
    destination = directory / 'settings-v2.ini'
    _check_paths(source, destination)
    if destination.exists():
        raise ValueError('CowType v2 settings already exist; no files were changed')
    if not source.exists():
        raise ValueError('CowType v1 settings were not found; no files were changed')
    if source.stat().st_size > 65536:
        raise ValueError('CowType v1 settings file is too large')

    parser = configparser.ConfigParser(interpolation=None, strict=True)
    parser.optionxform = str
    parser.read_string(source.read_text(encoding='utf-8'))
    if parser.sections() != ['CowType'] or parser['CowType'].get('Version') != '1':
        raise ValueError('The source file is not a supported CowType v1 configuration')

    old = dict(parser['CowType'])
    values = settings.DEFAULTS.copy()
    values['Version'] = '2'
    for key in values.keys() - {*settings.HOTKEYS, 'TraditionalHotkey', 'Version'}:
        if key in old:
            values[key] = old[key]

    for key in settings.HOTKEYS[:-1]:
        value = old.get(key, '')
        if key == 'ModeHotkey' and not value:
            value = 'Shift' if old.get('ShiftToggle', '').lower() == 'true' else 'None'
        values[key] = REPLACEMENTS[key] if value == OLD_HOTKEY_DEFAULTS[key] else (value or settings.DEFAULTS[key])
    values['TraditionalHotkey'] = settings.DEFAULTS['TraditionalHotkey']

    warnings = []
    for index, key in enumerate(settings.HOTKEYS):
        value = settings.canonical_shortcut(values[key])
        if any(settings._shortcut_conflict(value, values[other])
               for other in settings.HOTKEYS[:index]):
            if key != 'TraditionalHotkey':
                raise ValueError(f'Existing shortcuts conflict at {key}; no files were changed')
            value = 'None'
            warnings.append('TraditionalHotkey disabled because it conflicts with an existing shortcut')
        values[key] = value
    settings.validate(values)

    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.settings-v2-', dir=directory)
    backup = _backup_path(source)
    try:
        with os.fdopen(fd, 'w', encoding='utf-8') as stream:
            stream.write('[CowType]\n' + ''.join(f'{key}={value}\n' for key, value in values.items()))
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(source, backup)
        try:
            os.replace(temporary, destination)
        except OSError:
            os.replace(backup, source)
            raise
        dirfd = os.open(directory, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(dirfd)
        finally:
            os.close(dirfd)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    print(f'Migrated settings to {destination}')
    print(f'Original settings moved to {backup}')
    for warning in warnings:
        print(f'Warning: {warning}', file=sys.stderr)


def restart_fcitx():
    try:
        result = subprocess.run(['fcitx5-remote', '-r'], check=False, timeout=10)
    except (OSError, subprocess.TimeoutExpired) as error:
        print(f'cowtype-config-migrate: could not restart Fcitx5: {error}', file=sys.stderr)
        return False
    if result.returncode:
        print('cowtype-config-migrate: Fcitx5 restart request failed', file=sys.stderr)
        return False
    print('Fcitx5 restart requested')
    return True


if __name__ == '__main__':
    try:
        migrate()
        try:
            answer = input('Restart Fcitx5 now? [Y/n] ').strip().lower()
        except EOFError:
            answer = ''
        if answer in ('', 'y', 'yes'):
            restart_fcitx()
        else:
            print('Fcitx5 was not restarted; restart it to load the migrated settings.')
    except (OSError, ValueError, configparser.Error) as error:
        print(f'cowtype-config-migrate: {error}', file=sys.stderr)
        raise SystemExit(2)
