#!/usr/bin/env python3
"""Fetch an explicitly selected free text model; Python 3.9+, standard library only.

No inference, code execution, package installation or automatic enablement.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import sys
import tempfile
import urllib.parse
import urllib.request
from pathlib import Path

CATALOG = Path(__file__).with_name('catalog.json')
AGENT = 'CowType-model-preparation/1'


def load_model(name):
    catalog = json.loads(CATALOG.read_text())
    if catalog.get('format') != 1 or name not in catalog.get('models', {}):
        raise ValueError('Unknown catalog format/model; use a reviewed catalog entry')
    model = catalog['models'][name]
    if (model['source'] != 'modelscope' or model['license'] != 'Apache-2.0'
            or not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', model['repository'])
            or not re.fullmatch(r'[a-f0-9]{40}', model['revision'])):
        raise ValueError('Unreviewed source, license or revision')
    paths = set()
    for item in model['files']:
        path = item['path']
        if (not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*', path)
                or path in {'.', '..', 'SOURCE.json'} or path in paths
                or not re.fullmatch(r'[a-f0-9]{64}', item['sha256'])
                or not isinstance(item['size'], int) or item['size'] <= 0):
            raise ValueError('Unsafe path, size or digest in model catalog')
        paths.add(path)
    if 'LICENSE' not in paths or not any(p.endswith('.gguf') for p in paths):
        raise ValueError('Catalog must provide a license and a model')
    return model


def url_for(model, item):
    query = urllib.parse.urlencode({'Revision': model['revision'], 'FilePath': item['path']})
    return f"https://modelscope.cn/api/v1/models/{model['repository']}/repo?{query}"


def checksum(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def verified_file(path, item):
    return (not path.is_symlink() and path.is_file()
            and path.stat().st_size == item['size'] and checksum(path) == item['sha256'])


def fetch(model, item, target, timeout):
    request = urllib.request.Request(url_for(model, item), headers={'User-Agent': AGENT})
    h, total = hashlib.sha256(), 0
    with urllib.request.urlopen(request, timeout=timeout) as response:
        if urllib.parse.urlsplit(response.geturl()).scheme != 'https':
            raise ValueError('Refusing non-HTTPS model response')
        with target.open('xb') as output:
            while block := response.read(1024 * 1024):
                total += len(block)
                if total > item['size']:
                    raise ValueError(f"File exceeds catalog size: {item['path']}")
                output.write(block)
                h.update(block)
    if total != item['size'] or h.hexdigest() != item['sha256']:
        raise ValueError(f"Size/SHA-256 mismatch: {item['path']}")


def prepare(model, destination, timeout):
    if destination.is_symlink():
        raise ValueError('Model output cannot be a symlink')
    if destination.exists():
        if not destination.is_dir() or not all(
                verified_file(destination / f['path'], f) for f in model['files']):
            raise ValueError('Existing output differs from reviewed model; select a new directory')
        return destination.resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    if shutil.disk_usage(destination.parent).free < sum(f['size'] for f in model['files']) + 1048576:
        raise ValueError('Insufficient free disk space')
    staging = Path(tempfile.mkdtemp(prefix='.cowtype-text-model-', dir=destination.parent))
    try:
        # License is fetched and verified before any model bytes are requested.
        for item in sorted(model['files'], key=lambda f: f['path'] != 'LICENSE'):
            print(f"Downloading {item['path']} ({item['size']} bytes)", file=sys.stderr)
            fetch(model, item, staging / item['path'], timeout)
        source = {'repository': model['repository'], 'revision': model['revision'],
                  'license': model['license'], 'source': model['source'],
                  'files': model['files']}
        (staging / 'SOURCE.json').write_text(json.dumps(source, indent=2) + '\n')
        if destination.exists() or destination.is_symlink():
            raise ValueError('Output appeared during download; refusing to overwrite')
        staging.rename(destination)
        return destination.resolve()
    finally:
        if staging.exists():
            shutil.rmtree(staging)


def select_model(model, directory):
    root = Path(os.environ.get('XDG_CONFIG_HOME', str(Path.home() / '.config'))) / 'cowtype'
    for component in (root, *root.absolute().parents):
        if component.is_symlink():
            raise ValueError('Model selection path must not contain symlinks')
    root.mkdir(parents=True, exist_ok=True)
    target = root / 'text-model.json'
    if target.exists() or target.is_symlink():
        raise ValueError('Selection already exists; preserve it and choose explicitly in the future settings UI')
    entry = next(f for f in model['files'] if f['path'].endswith('.gguf'))
    selection = {'format': 1, 'enabled': False, 'backend': 'llama.cpp',
                 'model': str(directory / entry['path']), 'license': model['license'],
                 'repository': model['repository'], 'revision': model['revision'],
                 'sha256': entry['sha256']}
    # Exclusive create protects existing user settings; leave no partial file on failure.
    fd = os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    try:
        with os.fdopen(fd, 'w') as stream:
            stream.write(json.dumps(selection, ensure_ascii=False, indent=2) + '\n')
    except BaseException:
        target.unlink(missing_ok=True)
        raise
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--model', default='qwen3-0.6b-q8', help='Reviewed catalog model ID')
    parser.add_argument('--output', type=Path, help='New directory; defaults to XDG_DATA_HOME/cowtype/models/<id>')
    parser.add_argument('--check', '--disable-download', dest='check', action='store_true',
                        help='Validate dependencies/catalog and print plan; no network or writes')
    parser.add_argument('--accept-license', action='store_true', help='Explicitly accept the listed model license before downloading')
    parser.add_argument('--select', action='store_true', help='Record disabled selection for future integration; never enables inference')
    parser.add_argument('--timeout', type=float, default=60, help='Network operation timeout in seconds')
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error('--timeout must be positive')
    try:
        model = load_model(args.model)
        data = Path(os.environ.get('XDG_DATA_HOME', str(Path.home() / '.local/share')))
        destination = (args.output or data / 'cowtype/models' / args.model).expanduser().absolute()
        print(f"Model: {model['repository']} @ {model['revision']}\nLicense: {model['license']}\n"
              f"License URL: {url_for(model, next(f for f in model['files'] if f['path'] == 'LICENSE'))}\n"
              f"Output: {destination}\nModel bytes: {sum(f['size'] for f in model['files'])}\n"
              'Requires: Python 3.9+, HTTPS CA certificates, network and writable output for download; '
              'no inference/runtime installation performed.')
        if args.check:
            return 0
        if not args.accept_license:
            parser.error('Download requires --accept-license; use --check to preview without changes')
        directory = prepare(model, destination, args.timeout)
        if args.select:
            print(f'Recorded disabled selection: {select_model(model, directory)}')
        print(f'Ready on disk: {directory}; new host integration and validation are still required.')
        return 0
    except (OSError, ValueError, KeyError, StopIteration) as error:
        print(f'Model preparation failed: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
