# SPDX-License-Identifier: GPL-3.0-or-later
"""Delegate persistent secrets to the desktop Secret Service through libsecret."""
import hashlib
import os
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


def resolve(settings):
    if settings['key_source'] == 'none':
        return ''
    if settings['key_source'] == 'environment':
        key = os.environ.get('COWTYPE_TRANSLATE_KEY', '')
    else:
        Secret, schema = _service()
        try:
            key = Secret.password_lookup_sync(schema, _attributes(settings), None)
        except Exception:
            raise TranslationError('keyring') from None
    if not valid_key(key):
        raise TranslationError('key')
    return key
