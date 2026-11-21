#!/usr/bin/env python3
"""Verify project source inventory, hashes and local Markdown links (Python 3.9+).

Use --refresh after reviewing edits. Local builds and user data are never exported.
"""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
from datetime import date
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST_REL = 'tooling/SOURCE-MANIFEST.json'
BUILD_DIRS = {'target', 'build', 'dist', '.venv', '.cargo-home', 'resources'}
ENV_DIRS = {'.git', '.agents', '.aws', '.codex'}
ROOT_FILES = {'.gitignore', '.gitattributes', 'AGENTS.md', 'LICENSE', 'NOTICE.md',
              'README.md', 'STRUCTURES.md', 'Cargo.toml', 'Cargo.lock', 'CMakeLists.txt',
              'CHANGELOG.md', 'build-arch.sh'}
ROOT_DIRS = {'LICENSES', 'assets', 'docs', 'i18n', 'packaging', 'tests', 'tooling', '.github'}
EXCLUDED_SUFFIXES = {'.onnx', '.pt', '.pth', '.bin', '.gguf', '.safetensors',
                     '.so', '.a', '.o', '.wcache', '.log', '.zip', '.tar', '.gz'}


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def inventory(root):
    paths = []
    for top in root.iterdir():
        if top.name in ENV_DIRS:
            continue
        if top.name == 'cowtype-state' and top.is_symlink():
            continue  # User diagnostic attachment, never exported or traversed.
        if top.is_symlink():
            raise ValueError(f'Symlink not allowed: {top.name}')
        if top.name in BUILD_DIRS and top.is_dir():
            continue
        if (top.name not in ROOT_FILES | ROOT_DIRS
                and not (top.is_dir() and (top.name == 'src' or top.name.startswith('src-')))):
            raise ValueError(f'Unexpected project root: {top.name}')
        for path in [top, *top.rglob('*')] if top.is_dir() else [top]:
            rel = path.relative_to(root)
            if path.is_symlink():
                raise ValueError(f'Symlink not allowed: {rel}')
            if '__pycache__' in rel.parts or path.suffix in {'.pyc', '.pyo'}:
                continue
            if any(part in BUILD_DIRS or part in ENV_DIRS for part in rel.parts):
                raise ValueError(f'Generated/private directory in source: {rel}')
            if path.is_file() and rel.as_posix() != MANIFEST_REL:
                if path.suffix.lower() in EXCLUDED_SUFFIXES or path.stat().st_size > 5 * 1024 * 1024:
                    raise ValueError(f'Model/build/archive or oversized source: {rel}')
                paths.append(path)
    return sorted(paths, key=lambda p: p.relative_to(root).as_posix())


def verify_links(paths, root):
    for path in paths:
        if path.suffix != '.md':
            continue
        for target in re.findall(r'(?<!!)\[[^\]]*\]\(([^)]+)\)', path.read_text()):
            target = target.split('#')[0]
            if not target or re.match(r'[a-z]+:', target):
                continue
            resolved = (path.parent / target).resolve()
            if not resolved.is_relative_to(root) or not resolved.exists():
                raise ValueError(f'Broken/escaping Markdown link in {path.relative_to(root)}: {target}')


def snapshot(paths, root):
    return [{'path': p.relative_to(root).as_posix(), 'sha256': sha(p), 'bytes': p.stat().st_size}
            for p in paths]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--refresh', action='store_true', help='Update the source manifest after reviewing edits')
    args = parser.parse_args()
    try:
        paths = inventory(ROOT)
        verify_links(paths, ROOT)
        rows = snapshot(paths, ROOT)
        manifest = ROOT / MANIFEST_REL
        if args.refresh:
            manifest.write_text(json.dumps({'format': 1, 'reviewed_on': date.today().isoformat(),
                'scope': 'Project source snapshot; excludes build output, models and user data',
                'files': rows}, ensure_ascii=False, indent=2) + '\n')
        else:
            recorded = json.loads(manifest.read_text())
            if recorded.get('format') != 1 or recorded.get('files') != rows:
                raise ValueError('Source inventory/hash mismatch; review edits before --refresh')
        print(f'Project source hashes, layout and links verified: {len(rows)} files + manifest')
        return 0
    except (OSError, ValueError, KeyError) as error:
        print(f'Source check failed: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
