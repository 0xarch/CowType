# SPDX-License-Identifier: GPL-3.0-or-later
"""Common UI catalogs; diagnostics always use en_US, independent of UI locale."""
import os
from pathlib import Path


def _root():
    source = Path(__file__).resolve().parents[2] / 'i18n'
    return source if (source / 'en_US.tsv').exists() else Path('/usr/share/cowtype/i18n')


def _read(name):
    return {key: text.replace(r'\n', '\n')
            for key, text in (line.split('\t', 1) for line in (_root() / (name + '.tsv')).read_text().splitlines())}


EN = _read('en_US')
ZH = _read('zh_CN')


def tr(key, **values):
    locale = next((os.environ[k] for k in ('COWTYPE_LANGUAGE', 'LC_ALL', 'LC_MESSAGES', 'LANG') if os.environ.get(k)), '')
    text = (ZH if locale.startswith('zh') else EN).get(key, EN.get(key, key))
    # One pass: replacement values containing braces are never parsed again.
    return text.format_map(values) if values else text


def en(key):
    return EN.get(key, key)
