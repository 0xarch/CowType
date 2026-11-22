# SPDX-License-Identifier: GPL-3.0-or-later
"""Private learning snapshots and optimistic requests; never rewrite the live TSV."""
from dataclasses import dataclass
from pathlib import Path
import re
import time
import uuid

@dataclass(frozen=True)
class Record:
    reading: str
    text: str
    count: int
    last: int
    token: str

    def weight(self, now=None):
        now = int(time.time()) if now is None else now
        return self.count >> min(max(0, now - self.last) // (30 * 86400), 31)


def valid(reading, text):
    return (bool(re.fullmatch(r'[a-z]+(?: [a-z]+)*', reading))
            and 0 < len(text) <= 64 and not text.isascii()
            and not any(ord(c) < 32 or 127 <= ord(c) <= 159 for c in text))


def load(directory):
    path = Path(directory) / 'learning-v1.tsv'
    if path.is_symlink():
        raise ValueError('learning.error.data')
    if not path.exists():
        return []
    if path.stat().st_size > 8 * 1024 * 1024:
        raise ValueError('learning.error.data')
    lines = path.read_text().splitlines()
    if not lines or lines[0] != '# cowtype-learning-v1':
        raise ValueError('learning.error.data')
    token = next((line.split('\t', 1)[1] for line in lines if line.startswith('# reset-token\t')), '')
    marker = Path(directory) / 'learning-reset-v1'
    if marker.exists() and marker.read_text().strip() != token:
        return []
    records = {}
    for line in lines[1:20004]:
        if len(records) >= 20000:
            break
        fields = line.split('\t')
        if len(fields) != 4 or not valid(fields[0], fields[1]):
            continue
        try:
            count, last = int(fields[2]), int(fields[3])
        except ValueError:
            continue
        if 0 < count <= 0xffffffff and 0 <= last <= 0xffffffffffffffff:
            record = Record(fields[0], fields[1], count, last, token)
            records[(record.reading, record.text)] = record
    return sorted(records.values(), key=lambda r: (-r.weight(), -r.count, r.reading, r.text))


def submit(directory, atomic, record, reading, text, count):
    if type(count) is not int or not 0 <= count <= 0xffffffff or (count and not valid(reading, text)):
        raise ValueError('learning.error.value')
    current = {(r.reading, r.text): r for r in load(directory)}
    if current.get((record.reading, record.text)) != record:
        raise ValueError('learning.error.conflict')
    if count and (reading, text) != (record.reading, record.text) and (reading, text) in current:
        raise ValueError('learning.error.conflict')
    request = uuid.uuid4().hex
    payload = ('# cowtype-learning-edit-v1\nreset-token\t' + record.token + '\n'
               + f'{record.reading}\t{record.text}\t{record.count}\t{record.last}\n'
               + f'{reading}\t{text}\t{count}\n')
    if len(payload.encode()) > 4096:
        raise ValueError('learning.error.value')
    atomic(Path(directory) / 'learning-edits' / (request + '.request'), payload)
    return request


def result(directory, request):
    if not re.fullmatch('[a-f0-9]{32}', request):
        raise ValueError('learning.error.value')
    path = Path(directory) / 'learning-edits' / (request + '.result')
    if not path.exists():
        return None
    if path.is_symlink() or path.stat().st_size > 32:
        raise ValueError('learning.error.data')
    status = path.read_text().strip()
    path.unlink()
    return {'applied': 'applied', 'conflict': 'learning.error.conflict',
            'invalid': 'learning.error.data'}.get(status, 'learning.error.data')
