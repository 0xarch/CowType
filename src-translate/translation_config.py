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
    if values['provider'] == 'bilibili' and (api != BILIBILI_API or model != BILIBILI_MODEL or values['key_source'] != 'none'):
        raise TranslationError('config')
    return dict(values)


def path():
    return Path(os.environ.get('XDG_CONFIG_HOME') or Path.home() / '.config') / 'cowtype/translate-v1.json'


def load():
    filename = path()
    try:
        with filename.open('rb') as stream:
            data = stream.read(16385)
        if len(data) > 16384:
            raise TranslationError('config')
        return validate(json.loads(data))
    except FileNotFoundError:
        return DEFAULTS.copy()
    except (OSError, ValueError, UnicodeError):
        raise TranslationError('config') from None


def save(values):
    values = validate(values)
    filename = path()
    if any(parent.is_symlink() for parent in (filename, *filename.parents)):
        raise TranslationError('config')
    filename.parent.mkdir(parents=True, mode=0o700, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.translate-', dir=filename.parent)
    try:
        with os.fdopen(fd, 'w') as stream:
            json.dump(values, stream, ensure_ascii=False)
            stream.write('\n')
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, filename)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
