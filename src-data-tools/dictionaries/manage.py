#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Pinned public Rime dictionaries: deterministic conversion and explicit optional install.

Does not execute YAML, upstream scripts, Rime binaries, models or user histories.
"""
import argparse
import datetime
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import tarfile
import tempfile
import unicodedata
import urllib.request

REGISTRY = Path(__file__).resolve().parents[1] / 'manifest/sources.json'
OWNER = 'cowtype-dictionaries-v1'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def registry():
    return json.loads(REGISTRY.read_text(encoding='utf-8'))['profiles']


def imports(data):
    """Read only the reviewed YAML import-list subset; never use a YAML object loader."""
    header = data.decode('utf-8').split('\n...')[0]
    result = []
    active = False
    for line in header.splitlines():
        if line.strip() == 'import_tables:':
            active = True
            continue
        if active and line and not line[0].isspace() and not line.startswith('#'):
            active = False
        if active:
            match = re.fullmatch(r'\s+-\s+([A-Za-z0-9_/-]+)\s*(?:#.*)?', line)
            if line.strip().startswith('-') and not match:
                raise ValueError('unsupported or unsafe dictionary import syntax')
            if match:
                value = match[1]
                if '..' in value.split('/') or value.startswith('/'):
                    raise ValueError('unsafe dictionary import')
                result.append(value + '.dict.yaml')
    return result


def normalize_reading(value):
    words = []
    for word in value.split():
        result = ''
        for char in unicodedata.normalize('NFD', word.lower().replace('u:', 'v')):
            if char == '\u0308' and result.endswith('u'):
                result = result[:-1] + 'v'
            elif char in '\u0300\u0301\u0304\u030c':
                continue
            elif 'a' <= char <= 'z':
                result += char
            else:
                return None
        if not result:
            return None
        if result.startswith(('jv', 'qv', 'xv', 'yv')):
            result = result[0] + 'u' + result[2:]
        words.append(result)
    return ' '.join(words) if words else None


def convert_table(data, scheme, output):
    counts = {'accepted': 0, 'skipped': 0, 'zero_weights_floored': 0}
    body = False
    for line in io.StringIO(data.decode('utf-8')):
        line = line.rstrip('\r\n')
        if not body:
            if line == '...':
                body = True
            continue
        if not line or line.startswith('#'):
            continue
        fields = line.split('\t')
        if len(fields) < 2 or not fields[0].strip() or any(ord(c) < 32 for c in fields[0]):
            counts['skipped'] += 1
            continue
        code = fields[1]
        weight = fields[2] if len(fields) > 2 else '1'
        if not re.fullmatch(r'[0-9]+', weight) or int(weight) > 2**64 - 1:
            counts['skipped'] += 1
            continue
        if scheme == 'wubi86':
            if not re.fullmatch('[a-y]{1,4}', code):
                counts['skipped'] += 1
                continue
        else:
            code = normalize_reading(code)
            if code is None:
                counts['skipped'] += 1
                continue
            if int(weight) == 0:
                weight = '1'
                counts['zero_weights_floored'] += 1
        output.write(f'{code}\t{fields[0]}\t{int(weight)}\n')
        counts['accepted'] += 1
    if not body:
        raise ValueError('missing Rime table header terminator')
    return counts


def check_bytes(data, record):
    if len(data) != record['bytes'] or digest(data) != record['sha256']:
        raise ValueError('upstream size/SHA-256 mismatch: ' + record['path'])


def fetch(profile, cache, cancel=None):
    """Single pinned GitHub archive, select regular whitelisted files without extraction."""
    safe_parent(cache)
    records = {r['path']: r for r in profile['files']}
    missing = []
    for path, record in records.items():
        dest = cache / path
        safe_parent(dest.parent)
        if dest.is_symlink():
            raise ValueError('source cache file must not be a symlink')
        if dest.is_file() and not dest.is_symlink():
            check_bytes(dest.read_bytes(), record)
        else:
            missing.append(path)
    if not missing:
        return
    url = f"https://codeload.github.com/{profile['repository']}/tar.gz/{profile['revision']}"
    request = urllib.request.Request(url, headers={'User-Agent': 'CowType-pinned-dictionaries/1'})
    # Download into a temporary regular file; bound archive size, do not trust Content-Length.
    with urllib.request.urlopen(request, timeout=90) as response, tempfile.TemporaryFile() as downloaded:
        total = 0
        while chunk := response.read(1024 * 1024):
            if cancel is not None and cancel.is_set():
                raise ValueError('download cancelled')
            total += len(chunk)
            if total > 150 * 1024 * 1024:
                raise ValueError('upstream archive exceeds 150 MiB limit')
            downloaded.write(chunk)
        downloaded.seek(0)
        with tarfile.open(fileobj=downloaded, mode='r|gz') as archive:
            for member in archive:
                path = '/'.join(member.name.split('/')[1:])
                if path not in missing:
                    continue
                if not member.isfile() or member.size != records[path]['bytes']:
                    raise ValueError('unexpected archive member: ' + path)
                data = archive.extractfile(member).read(member.size + 1)
                check_bytes(data, records[path])
                dest = cache / path
                safe_parent(dest.parent)
                # Exclusive creation also refuses links/files appearing after
                # the initial cache scan and preserves existing cache contents.
                with dest.open('xb') as output:
                    output.write(data)
                missing.remove(path)
    if missing:
        raise ValueError('missing pinned assets: ' + ', '.join(missing))


def prepare(name, profile, cache, destination):
    for record in profile['files']:
        check_bytes((cache / record['path']).read_bytes(), record)
    expected_imports = [p for p in profile['tables'] if p != profile['root']] + list(profile['excluded_imports'])
    if set(imports((cache / profile['root']).read_bytes())) != set(expected_imports):
        raise ValueError('upstream import graph differs from reviewed profile')
    destination.mkdir(parents=True, exist_ok=False)
    table = destination / 'lexicon.tsv'
    reports = {}
    with table.open('w', encoding='utf-8', newline='\n') as output:
        output.write(f'# cowtype-data: {name} 1 {profile["repository"]}@{profile["revision"]}\n')
        # Main table body is intentionally excluded for Ice: mixed Latin/unannotated rows.
        for path in profile['tables']:
            reports[path] = convert_table((cache / path).read_bytes(), profile['scheme'], output)
    if not sum(r['accepted'] for r in reports.values()):
        raise ValueError('no supported dictionary rows')
    # Preserve corresponding source, exact upstream licenses, authors and notices in package.
    for record in profile['files']:
        path = record['path']
        target = destination / 'source' / path
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(cache / path, target)
    data = table.read_bytes()
    manifest = {'format': 1, 'managed_by': OWNER, 'profile': name, 'upstream': profile,
                'tool': 'cowtype-dictionaries format 1', 'python_unicode_version': unicodedata.unidata_version,
                'transformations': ['TSV columns reordered', 'NFD tone marks removed; ü -> v; j/q/x/y + v -> u',
                                    'missing weight -> 1; pinyin zero weight -> 1',
                                    'unsupported/missing codes skipped and counted; Wubi stems ignored',
                                    'duplicates retained here; runtime merges pinyin by maximum frequency'],
                'tables': reports, 'output': {'path': 'lexicon.tsv', 'bytes': len(data), 'sha256': digest(data)},
                'acquired_on': datetime.date.today().isoformat()}
    (destination / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return manifest


def managed_path(data_home):
    return data_home / 'cowtype/dictionaries/wanxiang'


def ensure_managed(path):
    if path.is_symlink() or not path.is_dir():
        raise ValueError('refusing to modify a symlink or non-directory')
    manifest = json.loads((path / 'manifest.json').read_text(encoding='utf-8'))
    if manifest.get('managed_by') != OWNER or manifest.get('profile') != 'wanxiang':
        raise ValueError('refusing to modify an unmanaged dictionary directory')
    # shutil.rmtree is symlink-resistant on supported Linux Python; also reject links explicitly.
    if any(p.is_symlink() for p in path.rglob('*')):
        raise ValueError('refusing managed directory containing symlinks')
    expected = {'manifest.json', 'lexicon.tsv'} | {
        'source/' + record['path'] for record in manifest.get('upstream', {}).get('files', [])}
    if any(p.is_file() and p.relative_to(path).as_posix() not in expected for p in path.rglob('*')):
        raise ValueError('refusing managed directory containing unrelated files; move them out first')
    return manifest


def safe_parent(path):
    for part in [path, *path.parents]:
        if part.is_symlink():
            raise ValueError('managed dictionary path must not contain symlinks')
    path.mkdir(parents=True, exist_ok=True)


def install_optional(data_home, source_cache=None, cancel=None):
    target = managed_path(data_home)
    safe_parent(target.parent)
    if target.exists() or target.is_symlink():
        ensure_managed(target)
        raise ValueError('Wanxiang is already installed; remove it explicitly before reinstalling')
    profile = registry()['wanxiang']
    with tempfile.TemporaryDirectory(prefix='.wanxiang-', dir=target.parent) as staging:
        staging = Path(staging)
        cache = source_cache if source_cache is not None else staging / 'download'
        fetch(profile, cache, cancel)
        if cancel is not None and cancel.is_set():
            raise ValueError('installation cancelled')
        manifest = prepare('wanxiang', profile, cache, staging / 'ready')
        if cancel is not None and cancel.is_set():
            raise ValueError('installation cancelled')
        os.rename(staging / 'ready', target)
    return manifest


def remove_optional(data_home):
    path = managed_path(data_home)
    safe_parent(path.parent)
    if not path.exists() and not path.is_symlink():
        return False
    ensure_managed(path)
    # Rename atomically away from the runtime path before deleting only our own directory.
    with tempfile.TemporaryDirectory(prefix='.wanxiang-remove-', dir=path.parent) as staging:
        retired = Path(staging) / 'retired'
        path.rename(retired)
        shutil.rmtree(retired)
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-home', type=Path, default=Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share')))
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('status')
    sub.add_parser('remove-wanxiang')
    install = sub.add_parser('install-wanxiang')
    install.add_argument('--source-cache', type=Path)
    build = sub.add_parser('build-default')
    build.add_argument('--source-cache', type=Path, required=True)
    build.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == 'status':
            path = managed_path(args.data_home)
            installed = path.exists()
            manifest = ensure_managed(path) if installed else None
            print(json.dumps({'installed': installed, 'revision': manifest['upstream']['revision'] if manifest else None}))
        elif args.command == 'remove-wanxiang':
            print(json.dumps({'removed': remove_optional(args.data_home)}))
        elif args.command == 'install-wanxiang':
            print(json.dumps(install_optional(args.data_home, args.source_cache)['output']))
        else:
            for name, profile in registry().items():
                if not profile['default']:
                    continue
                cache = args.source_cache / profile['repository'].split('/')[-1]
                fetch(profile, cache)
                prepare(name, profile, cache, args.output / name)
            print('Prepared pinned Ice and Wubi, with upstream sources and licenses; no Wanxiang or models bundled.')
    except (OSError, ValueError, KeyError, tarfile.TarError) as error:
        parser.exit(1, f'dictionary operation failed: {error}\n')


if __name__ == '__main__':
    main()
