# SPDX-License-Identifier: GPL-3.0-or-later
"""Delegate persistent secrets to the desktop Secret Service through libsecret."""
import hashlib
import os
from pathlib import Path
from translation_config import TranslationError, endpoint


def _service():
    try:
        import gi
        gi.require_version('Secret', '1')
        from gi.repository import Secret
        schema = Secret.Schema.new('io.github._0xarch.CowType.Translation', Secret.SchemaFlags.NONE,
                                   {'endpoint': Secret.SchemaAttributeType.STRING})
        return Secret, schema
    except (ImportError, ValueError):
        raise TranslationError('keyring') from None


def _attributes(settings):
    # Bind each stored key to the exact API endpoint; changing providers must
    # never silently send the old provider's credential to the new endpoint.
    return {'endpoint': hashlib.sha256(endpoint(settings['api']).encode()).hexdigest()}


def store(settings, key):
    if not valid_key(key):
        raise TranslationError('key')
    Secret, schema = _service()
    try:
        if not Secret.password_store_sync(schema, _attributes(settings), Secret.COLLECTION_DEFAULT,
                                          'CowType translation API key', key, None):
            raise TranslationError('keyring')
    except Exception:
        raise TranslationError('keyring') from None


def remove(settings):
    Secret, schema = _service()
    try:
        Secret.password_clear_sync(schema, _attributes(settings), None)
    except Exception:
        raise TranslationError('keyring') from None


def valid_key(value):
    return isinstance(value, str) and bool(value.strip()) and len(value) <= 4096 and all(32 <= ord(c) < 127 for c in value)


def _fcitx_owner():
    """Resolve only the current user's active Fcitx service, never scan processes."""
    import gi
    gi.require_version('Gio', '2.0')
    from gi.repository import Gio, GLib
    bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    def call(method, name):
        return bus.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus',
                             method, GLib.Variant('(s)', (name,)), None, Gio.DBusCallFlags.NONE,
                             1500, None).unpack()[0]
    owner = call('GetNameOwner', 'org.fcitx.Fcitx5')
    uid = call('GetConnectionUnixUser', owner)
    if uid != os.getuid():
        raise TranslationError('environment')
    return owner, call('GetConnectionUnixProcessID', owner)


def _process_environment_key(pid):
    # Only this one named variable is returned; other environment entries are
    # neither exposed nor persisted. Bound the read even for a corrupt /proc.
    with Path('/proc', str(pid), 'environ').open('rb') as stream:
        data = stream.read(1048577)
    if len(data) > 1048576:
        return ''
    for item in data.split(b'\0'):
        if item.startswith(b'COWTYPE_TRANSLATE_KEY='):
            return item.split(b'=', 1)[1].decode('utf-8')
    return ''


def environment_key():
    value = os.environ.get('COWTYPE_TRANSLATE_KEY')
    if value is not None:
        return value
    try:
        owner = _fcitx_owner()
        value = _process_environment_key(owner[1])
        # Reject an owner change while reading; do not attach a stale process's
        # environment to a newly started Fcitx session.
        return value if _fcitx_owner() == owner else ''
    except Exception:
        return ''


def resolve(settings):
    if settings['key_source'] == 'none':
        return ''
    if settings['key_source'] == 'environment':
        key = environment_key()
        if not valid_key(key):
            raise TranslationError('environment')
    else:
        Secret, schema = _service()
        try:
            key = Secret.password_lookup_sync(schema, _attributes(settings), None)
        except Exception:
            raise TranslationError('keyring') from None
    if not valid_key(key):
        raise TranslationError('key')
    return key
