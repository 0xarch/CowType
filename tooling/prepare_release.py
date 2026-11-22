#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate collected native build assets before creating a release tag."""
import hashlib
from pathlib import Path
import re
import sys
import tomllib


def prepare(directory, version):
    if not re.fullmatch(r'\d+\.\d+\.\d+', version):
        raise ValueError('Expected numeric release version')
    expected = {
        f'cowtype-{version}-1-x86_64.pkg.tar.zst',
        f'cowtype-{version}-1.fedora45.x86_64.rpm',
        f'cowtype-{version}-1.fedora45.aarch64.rpm',
        f'cowtype-{version}-1.opensusetumbleweed.x86_64.rpm',
        f'cowtype_{version}-1.debianunstable_amd64.deb',
        f'cowtype_{version}-1.debianunstable_arm64.deb',
        f'cowtype_{version}-1.ubuntu26.04_amd64.deb',
        f'cowtype_{version}-1.ubuntu26.04_arm64.deb',
        f'cowtype_{version}-1.deepin23_amd64.deb',
        f'cowtype_{version}-1.deepin25_amd64.deb',
        f'cowtype_{version}-1.deepin25_arm64.deb',
        f'cowtype_{version}-1.deepin25_loong64.deb',
        f'cowtype-{version}-1.opensuseleap16.0.x86_64.rpm',
        f'cowtype_{version}-1.openkylin_amd64.deb',
        f'cowtype-{version}-source.tar.gz',
        'PKGBUILD',
    }
    actual = {path.name for path in directory.iterdir()}
    if actual != expected:
        raise ValueError('Missing or unexpected release assets')
    checksums = []
    for name in sorted(expected):
        path = directory / name
        if path.is_symlink() or not path.is_file() or not path.stat().st_size:
            raise ValueError('Invalid release asset: ' + name)
        digest = hashlib.sha256()
        with path.open('rb') as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b''):
                digest.update(block)
        checksums.append(f'{digest.hexdigest()}  {name}\n')
    (directory / 'SHA256SUMS').write_text(''.join(checksums))


if __name__ == '__main__':
    version = tomllib.loads((Path(__file__).resolve().parents[1] / 'Cargo.toml').read_text())['package']['version']
    if len(sys.argv) != 2:
        raise SystemExit('Usage: prepare_release.py ASSET_DIRECTORY')
    prepare(Path(sys.argv[1]), version)
