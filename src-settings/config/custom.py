# SPDX-License-Identifier: GPL-3.0-or-later
"""Private explicit shortcuts, atomic autosave and optimistic record edits."""
from dataclasses import dataclass
import fcntl
import os
from pathlib import Path
import re

HEADER = '# cowtype-custom-v1\n'
LIMIT = 8 * 1024 * 1024

@dataclass(frozen=True)
class Record:
    trigger: str
    text: str
    weight: int


def valid(record):
    return (bool(re.fullmatch('[a-z]{1,64}', record.trigger))
            and bool(record.text.strip()) and len(record.text) <= 4096
            and not any((ord(c) < 32 or 127 <= ord(c) <= 159) and c not in '\n\t' for c in record.text)
            and type(record.weight) is int and 1 <= record.weight <= 1_000_000)


def escape(text):
    return text.replace('\\', '\\\\').replace('\n', '\\n').replace('\t', '\\t')


def unescape(text):
    result, index = [], 0
    while index < len(text):
        if text[index] == '\\':
            index += 1
            if index == len(text) or text[index] not in 'nt\\':
                raise ValueError('custom.error.data')
            result.append({'n': '\n', 't': '\t', '\\': '\\'}[text[index]])
        else:
            result.append(text[index])
        index += 1
    return ''.join(result)


def load(directory):
    path = Path(directory) / 'custom-v1.tsv'
    if any(p.is_symlink() for p in (path, *path.parents)):
        raise ValueError('custom.error.data')
    if not path.exists():
        return []
    if path.stat().st_size > LIMIT:
        raise ValueError('custom.error.data')
    text = path.read_text(encoding='utf-8')
    if not text.startswith(HEADER):
        raise ValueError('custom.error.data')
    records, seen = [], set()
    for line in text[len(HEADER):].split('\n'):
        if not line:
            continue
        parts = line.split('\t')
        if len(parts) != 3 or not parts[2].isascii() or not parts[2].isdigit():
            raise ValueError('custom.error.data')
        record = Record(parts[0], unescape(parts[1]), int(parts[2]))
        key = (record.trigger, record.text)
        if not valid(record) or key in seen or len(records) >= 10000:
            raise ValueError('custom.error.data')
        seen.add(key)
        records.append(record)
    return records


def change(directory, atomic, old, new):
    """None old adds, None new deletes; reject stale edits and duplicate keys."""
    if new is not None and not valid(new):
        raise ValueError('custom.error.value')
    directory = Path(directory)
    if any(p.is_symlink() for p in (directory, *directory.parents)):
        raise ValueError('custom.error.data')
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    fd = os.open(directory / 'custom-writer.lock', os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX)
        records = load(directory)
        if old is not None and old not in records:
            raise ValueError('custom.error.conflict')
        if new is not None and any((r.trigger, r.text) == (new.trigger, new.text) and r != old for r in records):
            raise ValueError('custom.error.conflict')
        if old is None:
            if new is not None: records.append(new)
        elif new is None:
            records.remove(old)
        else:
            records[records.index(old)] = new
        if len(records) > 10000:
            raise ValueError('custom.error.limit')
        payload = HEADER + ''.join(f'{r.trigger}\t{escape(r.text)}\t{r.weight}\n' for r in records)
        if len(payload.encode()) > LIMIT:
            raise ValueError('custom.error.limit')
        atomic(directory / 'custom-v1.tsv', payload)
    finally:
        os.close(fd)
