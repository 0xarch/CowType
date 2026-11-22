#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare Rust 1.88 only when a disposable CI container's compiler is older."""
import hashlib
import os
from pathlib import Path
import platform
import re
import subprocess
import tempfile
import urllib.request

RUSTUP_VERSION = '1.28.2'
TOOLCHAIN = '1.88.0'
INSTALLER_SHA256 = {
    'x86_64': '20a06e644b0d9bd2fbdbfd52d42540bdde820ea7df86e92e533c073da0cdd43c',
    'aarch64': 'e3853c5a252fca15252d07cb23a1bdd9377a8c6f3efa01531109281ae47f841c',
    'loongarch64': '9861495ee3d7c5fd868635623188a1d6d7919ef6005b4aa9101eb67197fe6d93',
}


def main():
    current = subprocess.check_output(['rustc', '--version'], text=True).strip()
    match = re.match(r'rustc (\d+)\.(\d+)\.(\d+)', current)
    if not match:
        raise RuntimeError('Cannot determine Rust compiler version')
    print('Container compiler: ' + current, flush=True)
    if tuple(map(int, match.groups())) >= (1, 88, 0):
        return
    arch = platform.machine()
    if arch not in INSTALLER_SHA256:
        raise RuntimeError('No verified Rust installer for architecture: ' + arch)
    url = (f'https://static.rust-lang.org/rustup/archive/{RUSTUP_VERSION}/'
           f'{arch}-unknown-linux-gnu/rustup-init')
    with tempfile.TemporaryDirectory(prefix='cowtype-rust-bootstrap-') as temporary:
        installer = Path(temporary) / 'rustup-init'
        with urllib.request.urlopen(url, timeout=120) as response:
            installer.write_bytes(response.read())
        if hashlib.sha256(installer.read_bytes()).hexdigest() != INSTALLER_SHA256[arch]:
            raise RuntimeError('Rust installer checksum mismatch')
        installer.chmod(0o700)
        environment = dict(os.environ, RUSTUP_HOME='/opt/cowtype-rust/rustup',
                           CARGO_HOME='/opt/cowtype-rust/cargo', RUSTUP_INIT_SKIP_PATH_CHECK='yes')
        subprocess.run([str(installer), '-y', '--no-modify-path', '--profile', 'minimal',
                        '--default-toolchain', TOOLCHAIN], env=environment, check=True)


if __name__ == '__main__':
    main()
