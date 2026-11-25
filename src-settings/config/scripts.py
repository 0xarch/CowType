# SPDX-License-Identifier: GPL-3.0-or-later
"""Explicit Lua script storage; only enabled records enter the native index."""
import json
from pathlib import Path
import re
import secrets
import string

LIMIT = 128
MAX_SOURCE = 65536
HEADER = '# cowtype-scripts-v2\n'


def safe(path):
    path = Path(path)
    if any(parent.is_symlink() for parent in (path, *path.parents)):
        raise ValueError('scripts.error')
    return path


def validate(records):
    if not isinstance(records, list) or len(records) > LIMIT:
        raise ValueError('scripts.error')
    seen = set()
    for record in records:
        if not isinstance(record, dict) or set(record) != {'id', 'name', 'trigger', 'enabled', 'source', 'independent'}:
            raise ValueError('scripts.error')
        if (not isinstance(record['id'], str) or not re.fullmatch('[a-z]{16}', record['id'])
                or record['id'] in seen or not isinstance(record['name'], str)
                or not record['name'].strip() or len(record['name']) > 80
                or any(c in record['name'] for c in '\0\r\n')
                or not isinstance(record['trigger'], str) or not re.fullmatch('[a-z]{1,64}', record['trigger'])
                or type(record['independent']) is not bool or type(record['enabled']) is not bool or not isinstance(record['source'], str)
                or len(record['source'].encode('utf-8')) > MAX_SOURCE or '\0' in record['source']):
            raise ValueError('scripts.error')
        seen.add(record['id'])
    return records


def load(home):
    path = safe(Path(home) / 'scripts/records.json')
    if not path.exists():
        return []
    if path.stat().st_size > 12_000_000:
        raise ValueError('scripts.error')
    data = json.loads(path.read_text(encoding='utf-8'))
    if not isinstance(data, dict) or data.get('version') != 2:
        raise ValueError('scripts.migration')
    return validate(data['records'])


def save(home, atomic, records):
    validate(records)
    for record in records: validate_return(record['source'], record['independent'])
    root = safe(Path(home) / 'scripts')
    # Immutable code snapshots make index replacement atomic for the reader.
    snapshots = []
    for record in records:
        if record['enabled']:
            ident = ''.join(secrets.choice(string.ascii_lowercase) for _ in range(16))
            atomic(safe(root / (ident + '.lua')), record['source'])
            snapshots.append((ident, record['trigger'], int(record['independent'])))
    atomic(safe(root / 'records.json'), json.dumps({'version': 2, 'records': records}, ensure_ascii=False) + '\n')
    atomic(safe(root / 'index.tsv'), HEADER + ''.join(f'{ident}\t{trigger}\t{independent}\n' for ident, trigger, independent in snapshots))
    keep = {ident + '.lua' for ident, _, _ in snapshots}
    for path in root.glob('*.lua'):
        if re.fullmatch(r'[a-z]{16}\.lua', path.name) and path.name not in keep:
            safe(path).unlink()


def new_record(name='', trigger='', source='return {{5, input}}\n'):
    return dict(id=''.join(secrets.choice(string.ascii_lowercase) for _ in range(16)),
                name=name, trigger=trigger, enabled=False, source=source, independent=False)


EXAMPLES = {'date': ('riqi', False), 'time': ('shijian', False), 'system': ('xitong', True)}


def example(kind):
    if kind not in EXAMPLES:
        raise ValueError('scripts.error')
    root = Path(__file__).resolve().parents[2]
    paths = [root / 'src-scripts/examples' / (kind + '.lua'),
             Path('/usr/share/cowtype/scripts') / (kind + '.lua')]
    for path in paths:
        if path.is_file():
            return path.read_text(encoding='utf-8')
    if kind == 'system':
        raise ValueError('scripts.error')
    # The examples are tiny and independent of installation prefix.
    return "return {{5, os.date('%Y-%m-%d')}, {6, os.date('%Y年%m月%d日')}}\n" if kind == 'date' else "return {{5, os.date('%H:%M')}, {6, os.date('%H:%M:%S')}}\n"


class CheckError(ValueError):
    def __init__(self, key, line=0):
        super().__init__(key)
        self.key, self.line = key, line


class Token(str):
    def __new__(cls, text, line):
        value = super().__new__(cls, text)
        value.line = line
        return value


def _tokens(source):
    """Lex strings/comments separately so return-like text cannot spoof validation."""
    pattern = re.compile(r'''\s+|--\[(=*)\[|--[^\n]*|\[(=*)\[|'(?:\\.|[^'\\])*'|"(?:\\.|[^"\\])*"|(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?|[A-Za-z_][A-Za-z_0-9]*|.''', re.S)
    tokens, offset = [], 0
    while offset < len(source):
        match = pattern.match(source, offset)
        if not match:
            raise ValueError('scripts.error.return')
        line = source.count("\n", 0, offset) + 1
        text = match.group(); offset = match.end()
        if match.group(1) is not None or match.group(2) is not None:
            equals = match.group(1) if match.group(1) is not None else match.group(2)
            end = source.find(']' + equals + ']', offset)
            if end < 0: raise CheckError('scripts.check.unclosed', line)
            offset = end + len(equals) + 2
            if not text.startswith('--'): tokens.append(Token('<string>', line))
        elif text.isspace() or text.startswith('--'):
            continue
        elif text[0] in ('"', "'"):
            tokens.append(Token('<string>', line))
        else:
            tokens.append(Token(text, line))
    return tokens


def _block(stack, token):
    if token in ('function', 'if', 'for', 'while', 'repeat'):
        stack.append([token, token in ('for', 'while')])
    elif token == 'do':
        if stack and stack[-1][1]: stack[-1][1] = False
        else: stack.append(['do', False])
    elif token in ('end', 'until') and stack:
        stack.pop()


def _table(tokens, start):
    if start >= len(tokens) or tokens[start] != '{': return None, start
    fields, current, brackets, blocks = [], [], [], []
    for index in range(start + 1, len(tokens)):
        token = tokens[index]
        if not brackets and not blocks:
            if token == '}':
                if current: fields.append(current)
                return fields, index + 1
            if token in (',', ';'):
                if not current: return None, index
                fields.append(current); current = []
                continue
        if token in ('{', '(', '['): brackets.append(token)
        elif token in ('}', ')', ']'):
            if not brackets or brackets.pop() != {'}': '{', ')': '(', ']': '['}[token]: return None, index
        _block(blocks, token)
        current.append(token)
    return None, len(tokens)


def validate_return(source, independent):
    """Check explicit ranked return constructors without running user code.

    Dynamically computed text is allowed. Ranked mode requires literal finite
    ranks and explicit constructors at every chunk-level return; function-local
    returns are unrestricted. Runtime independently checks actual value types.
    """
    if independent: return
    import math
    tokens = _tokens(source)
    stack, found = [], False
    def ranked(fields):
        if fields is None or len(fields) != 2 or not fields[1]: return False
        rank = ''.join(fields[0])
        if not re.fullmatch(r'[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?', rank): return False
        try: return math.isfinite(float(rank))
        except ValueError: return False
    for index, token in enumerate(tokens):
        if token == 'return' and not any(kind == 'function' for kind, _ in stack):
            found = True
            fields, end = _table(tokens, index + 1)
            valid = fields is not None and (not fields or ranked(fields))
            if not valid and fields:
                valid = True
                for field in fields:
                    pair, consumed = _table(field, 0)
                    if consumed != len(field) or not ranked(pair): valid = False; break
            if fields is None:
                raise CheckError('scripts.check.return_table', getattr(token, 'line', 0))
            if not valid:
                raise CheckError('scripts.check.return_pair', getattr(token, 'line', 0))
            if end < len(tokens) and tokens[end] in (',', '.', '+', '-', '*', '/', '^', 'and', 'or', '['):
                raise CheckError('scripts.check.return_expression', getattr(token, 'line', 0))
        _block(stack, token)
    if not found: raise CheckError('scripts.check.no_return')


# Conservative hints, never a security boundary. Strings and comments are ignored.
def inspect(source, independent):
    validate_return(source, independent)
    tokens = _tokens(source)
    restricted = {'io', 'package', 'debug', 'require', 'load', 'loadfile', 'dofile',
                  'collectgarbage', '_G', '_ENV', 'fcitx', 'getmetatable', 'setmetatable',
                  'rawget', 'rawset', 'rawequal', 'pcall', 'xpcall', 'coroutine', 'print'}
    findings, seen = [], set()
    for index, token in enumerate(tokens):
        capability = None
        if token in restricted:
            # A field with this name is not necessarily a global capability.
            if index == 0 or tokens[index - 1] not in ('.', ':'):
                capability = str(token)
        elif token == 'os' and index + 2 < len(tokens):
            if tokens[index + 1] == '.' and tokens[index + 2] not in ('date', 'time'):
                capability = 'os.' + str(tokens[index + 2])
            elif tokens[index + 1] != '.':
                capability = 'os (dynamic access)'
        if capability and capability not in seen:
            seen.add(capability)
            findings.append((capability, token.line))
    return findings
