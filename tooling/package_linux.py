#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build distro-native Deb/RPM packages from a verified prepared source tree.
Run in the target distribution; dependency installation belongs to the caller.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile
import tomllib


def run(*command, **kwargs):
    return subprocess.run([str(arg) for arg in command], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--format', choices=('deb', 'rpm'), required=True)
    parser.add_argument('--distro', required=True)
    parser.add_argument('--libdir', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not re.fullmatch(r'[a-z][a-z0-9.]*', args.distro):
        parser.error('Invalid distribution identifier')
    if not re.fullmatch(r'lib(?:64|/[A-Za-z0-9_-]+)?', args.libdir):
        parser.error('Expected a relative system library directory')
    source = args.source.resolve()
    version = tomllib.loads((source / 'Cargo.toml').read_text())['package']['version']
    if not re.fullmatch(r'\d+\.\d+\.\d+', version):
        parser.error('Expected numeric release version')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    run('python3', source / 'tooling/development.py')
    with tempfile.TemporaryDirectory(prefix='cowtype-package-') as temporary:
        work = Path(temporary)
        build = work / 'build'
        stage = work / 'stage'
        run('cmake', '-S', source, '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
            '-DCMAKE_INSTALL_PREFIX=/usr', '-DCMAKE_INSTALL_LIBDIR=' + args.libdir, '-DBUILD_TESTING=OFF')
        run('cmake', '--build', build, '-j2')
        run('cmake', '--install', build, env=dict(os.environ, DESTDIR=str(stage)))
        elfs = []
        for path in sorted(stage.rglob('*')):
            if path.is_file():
                with path.open('rb') as stream:
                    if stream.read(4) == b'\x7fELF':
                        elfs.append(path)
                        run('strip', '--strip-unneeded', path)
        if args.format == 'deb':
            (work / 'debian').mkdir()
            (work / 'debian/control').write_text('Source: cowtype\n\nPackage: cowtype\nArchitecture: any\n')
            shared = subprocess.check_output(['dpkg-shlibdeps', '-O', *('-e' + str(p) for p in elfs)], cwd=work, text=True)
            dependencies = next(line.split('=', 1)[1] for line in shared.splitlines() if line.startswith('shlibs:Depends='))
            architecture = subprocess.check_output(['dpkg', '--print-architecture'], text=True).strip()
            template = (source / 'packaging/debian/control.in').read_text()
            for key, value in {'VERSION': version, 'DISTRO': args.distro, 'ARCH': architecture, 'SHLIBS': dependencies}.items():
                template = template.replace('@' + key + '@', value)
            (stage / 'DEBIAN').mkdir()
            (stage / 'DEBIAN/control').write_text(template)
            # Keep '~' in the Debian control version, but use a release-safe
            # filename so uploaded asset names still match SHA256SUMS.
            run('dpkg-deb', '--root-owner-group', '--build', stage, output / f'cowtype_{version}-1.{args.distro}_{architecture}.deb')
        else:
            for name in ('BUILD', 'BUILDROOT', 'RPMS', 'SOURCES', 'SPECS', 'SRPMS'):
                (work / name).mkdir()
            spec = (source / 'packaging/rpm/cowtype.spec.in').read_text()
            suse = args.distro.startswith('opensuse')
            ui_requires = ('python3, python3-gobject, typelib-1_0-Gtk-4_0 >= 4.10, typelib-1_0-Adw-1 >= 1.5'
                           if suse else 'python3, python3-gobject, gtk4 >= 4.10, libadwaita >= 1.5')
            emoji_font = 'google-noto-coloremoji-fonts' if suse else 'google-noto-color-emoji-fonts'
            for key, value in {'VERSION': version, 'DISTRO': args.distro, 'STAGE': str(stage), 'LIBDIR': args.libdir,
                               'UI_REQUIRES': ui_requires, 'EMOJI_FONT': emoji_font,
                               'SOURCEVIEW_PACKAGE': 'typelib-1_0-GtkSource-5' if suse else 'gtksourceview5',
                               'SECRET_PACKAGE': 'typelib-1_0-Secret-1' if suse else 'libsecret'}.items():
                spec = spec.replace('@' + key + '@', value)
            specfile = work / 'SPECS/cowtype.spec'
            specfile.write_text(spec)
            run('rpmbuild', '-bb', specfile, '--define', '_topdir ' + str(work),
                '--define', '_rpmdir ' + str(output), '--define', '_rpmfilename %{NAME}-%{VERSION}-%{RELEASE}.%{ARCH}.rpm')


if __name__ == '__main__':
    main()
