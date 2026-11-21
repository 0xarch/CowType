#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Export verified current sources plus pinned default data for offline makepkg.

Never reads Git objects, refs or user resources. Run the dictionary preparation
and source-inventory review first. Does not install, sign, push or release.
"""
import gzip
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import development

ROOT = Path(__file__).resolve().parents[1]
VERSION = '0.1.0'
EPOCH = 1794960000  # Fixed archive timestamp; not an upstream publication date.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dictionary-dir', type=Path, default=ROOT / 'build/data')
    parser.add_argument('--vendor-dir', type=Path, default=ROOT / 'build/voice-vendor')
    args = parser.parse_args()
    subprocess.run(['python3', str(ROOT / 'tooling/development.py')], check=True, cwd=ROOT)
    files = [(p, p.relative_to(ROOT).as_posix())
             for p in development.inventory(ROOT) + [ROOT / development.MANIFEST_REL]]
    registry = json.loads((ROOT / 'src-data-tools/manifest/sources.json').read_text())['profiles']
    for name in ('ice', 'wubi'):
        directory = args.dictionary_dir / name
        manifest = json.loads((directory / 'manifest.json').read_text())
        if manifest['upstream'] != registry[name]:
            raise ValueError('staged upstream profile mismatch: ' + name)
        expected = {'manifest.json', 'lexicon.tsv'}
        for record in registry[name]['files']:
            path = directory / 'source' / record['path']
            if path.is_symlink() or path.stat().st_size != record['bytes'] or development.sha(path) != record['sha256']:
                raise ValueError('staged source mismatch: ' + str(path))
            expected.add('source/' + record['path'])
        output = manifest['output']
        lexicon = directory / 'lexicon.tsv'
        if lexicon.stat().st_size != output['bytes'] or development.sha(lexicon) != output['sha256']:
            raise ValueError('staged lexicon mismatch: ' + name)
        actual = set()
        for path in directory.rglob('*'):
            if path.is_symlink():
                raise ValueError('symlink in staged data')
            if path.is_file():
                actual.add(path.relative_to(directory).as_posix())
                files.append((path, 'build/data/' + name + '/' + path.relative_to(directory).as_posix()))
        if actual != expected:
            raise ValueError('unexpected staged files: ' + name)
    # Cargo vendor records contain registry checksums and per-file digests.
    vendor = args.vendor_dir
    import tomllib
    locked = tomllib.loads((ROOT / 'src-voice/Cargo.lock').read_text())['package']
    expected_crates = {f"{r['name']}-{r['version']}": r for r in locked if 'checksum' in r}
    if {p.name for p in vendor.iterdir()} != set(expected_crates):
        raise ValueError('vendor crate set differs from voice lockfile')
    for name, record in expected_crates.items():
        directory = vendor / name
        checksums = json.loads((directory / '.cargo-checksum.json').read_text())
        if checksums['package'] != record['checksum']:
            raise ValueError('vendor registry checksum mismatch: ' + name)
        actual = {p.relative_to(directory).as_posix() for p in directory.rglob('*') if p.is_file()}
        if actual != set(checksums['files']) | {'.cargo-checksum.json'}:
            raise ValueError('unexpected vendor files: ' + name)
        for relative, digest in checksums['files'].items():
            path = directory / relative
            if path.is_symlink() or development.sha(path) != digest:
                raise ValueError('vendor digest mismatch: ' + str(path))
        files.extend((directory / relative, 'build/voice-vendor/' + name + '/' + relative)
                     for relative in actual)
    destination = ROOT / 'dist'
    destination.mkdir(exist_ok=True)
    archive = destination / f'cowtype-{VERSION}-source.tar.gz'
    # Explicit inventory only: no .git, build caches, personal files or optional data.
    with archive.open('wb') as raw, gzip.GzipFile(filename='', mode='wb', fileobj=raw, mtime=EPOCH) as compressed:
        with tarfile.open(fileobj=compressed, mode='w|', format=tarfile.PAX_FORMAT) as tar:
            for path, relative in sorted(files, key=lambda entry: entry[1]):
                info = tar.gettarinfo(str(path), arcname=f'cowtype-{VERSION}/' + relative)
                info.uid = info.gid = 0
                info.uname = info.gname = 'root'
                info.mtime = EPOCH
                info.mode = 0o755 if path.stat().st_mode & 0o111 else 0o644
                info.pax_headers = {}
                with path.open('rb') as source:
                    tar.addfile(info, source)
    sha = hashlib.sha256(archive.read_bytes()).hexdigest()
    build = ROOT / 'build/arch'
    build.mkdir(exist_ok=True)
    template = (ROOT / 'packaging/arch/PKGBUILD.in').read_text()
    (build / 'PKGBUILD').write_text(template.replace('@SOURCE_SHA256@', sha))
    shutil.copyfile(archive, build / archive.name)
    shutil.copyfile(build / 'PKGBUILD', destination / 'PKGBUILD')
    print(f'{archive}\nsha256={sha}\nBuild with: cd build/arch && PKGDEST="{destination}" makepkg --cleanbuild --force')


if __name__ == '__main__':
    main()
