# SPDX-License-Identifier: GPL-3.0-or-later
"""Non-secret translation settings. Only the API key may come from the environment."""
import ipaddress
import json
import os
from pathlib import Path
import tempfile
from urllib.parse import urlsplit, urlunsplit

BILIBILI_API = 'https://index-translate.bilibili.com/v1'
BILIBILI_MODEL = 'Index-Translate-35B-A3B'
BILIBILI_MODELS = ('Index-Translate-2B', 'Index-Translate-9B', 'Index-Translate-35B-A3B')
LANGUAGES = ('auto', 'zh', 'en', 'ja', 'ko', 'fr', 'de', 'es', 'ru', 'pt', 'it', 'ar', 'vi', 'th', 'id')
DEFAULTS = dict(version=1, provider='bilibili', api=BILIBILI_API, model=BILIBILI_MODEL,
                target='auto', key_source='none')

class TranslationError(Exception):
    """Only stable catalog keys cross process/UI boundaries, never request data."""
    def __init__(self, code):
        self.code = 'translate.error.' + code
        super().__init__(self.code)


def endpoint(api):
    try:
        url = urlsplit(api)
        loopback = url.hostname == 'localhost'
        if url.hostname and not loopback:
            try:
                loopback = ipaddress.ip_address(url.hostname).is_loopback
            except ValueError:
                pass
        if (url.scheme not in ('https', 'http') or not url.hostname or url.username is not None
                or url.password is not None or url.query or url.fragment
                or (url.scheme == 'http' and not loopback) or not url.port and url.netloc.endswith(':')
                or any(ord(c) < 33 or ord(c) == 127 for c in api)):
            raise ValueError()
        _ = url.port
        path = url.path.rstrip('/')
        if not path.endswith('/chat/completions'):
            path += '/chat/completions'
        return urlunsplit((url.scheme, url.netloc, path, '', ''))
    except (ValueError, TypeError):
        raise TranslationError('config') from None


def validate(values):
    if (not isinstance(values, dict) or set(values) != set(DEFAULTS)
            or values.get('version') != 1
            or values.get('provider') not in ('bilibili', 'custom')
            or values.get('target') not in LANGUAGES
            or values.get('key_source') not in ('none', 'environment', 'secret')):
        raise TranslationError('config')
    api, model = values['api'], values['model']
    if (not isinstance(api, str) or len(api) > 2048 or not isinstance(model, str)
            or not model.strip() or len(model) > 256 or any(ord(c) < 32 or ord(c) == 127 for c in model)):
        raise TranslationError('config')
    endpoint(api)
    if values['provider'] == 'bilibili' and (api != BILIBILI_API or model not in BILIBILI_MODELS or values['key_source'] != 'none'):
        raise TranslationError('config')
    return dict(values)


def path():
    return Path(os.environ.get('XDG_CONFIG_HOME') or Path.home() / '.config') / 'cowtype/translate-v2.json'


def profile_defaults():
    return dict(version=2, provider='bilibili', target='auto',
                bilibili=dict(model=BILIBILI_MODEL),
                custom=dict(api='', model='', key_source='environment'))


def settings_for(profiles, provider=None):
    provider = profiles['provider'] if provider is None else provider
    if provider == 'bilibili':
        values = dict(DEFAULTS, model=profiles['bilibili']['model'], target=profiles['target'])
    else:
        values = dict(version=1, provider='custom', target=profiles['target'], **profiles['custom'])
    return validate(values)


def validate_profiles(profiles):
    if (not isinstance(profiles, dict) or set(profiles) != set(profile_defaults())
            or type(profiles.get('version')) is not int or profiles['version'] != 2
            or profiles['provider'] not in ('bilibili', 'custom') or profiles['target'] not in LANGUAGES
            or not isinstance(profiles['bilibili'], dict) or set(profiles['bilibili']) != {'model'}
            or profiles['bilibili']['model'] not in BILIBILI_MODELS
            or not isinstance(profiles['custom'], dict) or set(profiles['custom']) != {'api', 'model', 'key_source'}):
        raise TranslationError('config')
    custom = profiles['custom']
    if (custom['key_source'] not in ('none', 'environment', 'secret')
            or not isinstance(custom['api'], str) or len(custom['api']) > 2048
            or not isinstance(custom['model'], str) or len(custom['model']) > 256
            or any(ord(c) < 32 or ord(c) == 127 for c in custom['api'] + custom['model'])):
        raise TranslationError('config')
    # Inactive custom form drafts may be incomplete. Only the selected service
    # must be callable; switching to a preset must not erase the custom fields.
    settings_for(profiles)
    return dict(profiles, bilibili=dict(profiles['bilibili']), custom=dict(custom))


def _read(filename):
    with filename.open('rb') as stream:
        data = stream.read(16385)
    if len(data) > 16384:
        raise TranslationError('config')
    return json.loads(data)


def load_profiles():
    try:
        if path().exists():
            return validate_profiles(_read(path()))
        legacy = path().with_name('translate-v1.json')
        if not legacy.exists():
            return profile_defaults()
        values = validate(_read(legacy))
        profiles = profile_defaults()
        profiles.update(provider=values['provider'], target=values['target'])
        if values['provider'] == 'bilibili':
            profiles['bilibili']['model'] = values['model']
        else:
            profiles['custom'] = {key: values[key] for key in ('api', 'model', 'key_source')}
        return validate_profiles(profiles)
    except (OSError, ValueError, TypeError, UnicodeError):
        raise TranslationError('config') from None


def load():
    return settings_for(load_profiles())


def save_profiles(profiles):
    profiles = validate_profiles(profiles)
    filename = path()
    if any(parent.is_symlink() for parent in (filename, *filename.parents)):
        raise TranslationError('config')
    filename.parent.mkdir(parents=True, mode=0o700, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.translate-', dir=filename.parent)
    try:
        with os.fdopen(fd, 'w') as stream:
            json.dump(profiles, stream, ensure_ascii=False)
            stream.write('\n')
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, filename)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def save(values):
    values = validate(values)
    profiles = load_profiles()
    profiles.update(provider=values['provider'], target=values['target'])
    if values['provider'] == 'bilibili':
        profiles['bilibili']['model'] = values['model']
    else:
        profiles['custom'] = {key: values[key] for key in ('api', 'model', 'key_source')}
    save_profiles(profiles)
