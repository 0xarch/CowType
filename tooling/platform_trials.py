#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Inspect public container platforms and summarize optional distro trials."""
import argparse
import json
from pathlib import Path
import subprocess

TARGETS = [
    ('arch', 'x86_64', 'archlinux:latest', 'arch', 'existing'),
    ('fedora45', 'x86_64', 'fedora:45', 'rpm', 'existing'),
    ('debianunstable', 'x86_64', 'debian:unstable', 'deb', 'existing'),
    ('opensusetumbleweed', 'x86_64', 'opensuse/tumbleweed:latest', 'rpm', 'existing'),
    ('ubuntu26.04', 'x86_64', 'ubuntu:26.04', 'deb', 'deferred'),
    ('deepin23', 'x86_64', 'linuxdeepin/deepin:23', 'deb', 'deferred'),
    ('deepin25', 'x86_64', 'linuxdeepin/deepin:25', 'deb', 'deferred'),
    ('opensuseleap16.0', 'x86_64', 'registry.opensuse.org/opensuse/leap:16.0', 'rpm', 'deferred'),
    ('openkylin', 'x86_64', 'openkylin/openkylin:latest', 'deb', 'new-system'),
    ('ubuntu26.04', 'arm64', 'ubuntu:26.04', 'deb', 'new-architecture'),
    ('deepin25', 'arm64', 'linuxdeepin/deepin:crimson-arm64', 'deb', 'new-architecture'),
    ('debianunstable', 'arm64', 'debian:unstable', 'deb', 'new-architecture'),
    ('fedora45', 'arm64', 'fedora:45', 'rpm', 'new-architecture'),
    ('deepin25', 'loongarch64', 'linuxdeepin/deepin:crimson-loong64', 'deb', 'new-architecture'),
]


def select_targets(selection):
    if selection == 'all':
        return TARGETS
    requested = set(selection.split(','))
    known = {d + '-' + a for d, a, *_ in TARGETS}
    if not requested or requested - known:
        raise ValueError('Unknown trial targets: ' + ', '.join(sorted(requested - known)))
    return [row for row in TARGETS if row[0] + '-' + row[1] in requested]


def matrix(selection='all'):
    archs = {'x86_64': 'amd64', 'arm64': 'arm64', 'loongarch64': 'loong64'}
    return {'include': [dict(distro=d, arch=a, image=i, format=f, category=c,
                            platform='linux/' + archs[a],
                            runner='ubuntu-24.04-arm' if a == 'arm64' else 'ubuntu-24.04')
                        for d, a, i, f, c in select_targets(selection)]}


def platforms(value):
    result = set()
    if isinstance(value, dict):
        platform = value.get('platform')
        if isinstance(platform, dict) and platform.get('os') and platform.get('architecture'):
            result.add(platform['os'] + '/' + platform['architecture'])
        for nested in value.values():
            result.update(platforms(nested))
    elif isinstance(value, list):
        for nested in value:
            result.update(platforms(nested))
    return result


def inspect(image, platform, output):
    result = {'image': image, 'platform': platform, 'available': False, 'reason': ''}
    try:
        probe = subprocess.run(['docker', 'manifest', 'inspect', '--verbose', image],
                               capture_output=True, text=True, timeout=100)
        (output / 'manifest.log').write_text(probe.stdout + probe.stderr)
        if probe.returncode:
            result['reason'] = 'Container manifest unavailable; see manifest.log (not a source build failure)'
        else:
            found = platforms(json.loads(probe.stdout))
            result['platforms'] = sorted(found)
            result['available'] = platform in found
            result['reason'] = '' if result['available'] else f'Container has no {platform} platform; available: {", ".join(sorted(found))}'
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        result['reason'] = 'Container inspection failed: ' + type(error).__name__
    (output / 'image.json').write_text(json.dumps(result, indent=2) + '\n')
    print('available=' + str(result['available']).lower())


def record(directory, distro, arch, steps):
    image_file = directory / 'image.json'
    image = json.loads(image_file.read_text()) if image_file.exists() else {}
    stages = json.loads(steps)
    status, reason = 'passed', ''
    if not image.get('available'):
        status, reason = 'skipped', image.get('reason', 'Container inspection did not complete')
    else:
        for stage in ('qemu', 'container', 'probe', 'dependencies', 'build', 'verify'):
            if stages.get(stage) not in ('success', 'skipped'):
                status, reason = 'failed', stage + ' failed'
                break
    versions_file = directory / 'versions.txt'
    versions = versions_file.read_text() if versions_file.exists() else ''
    core = next((line.split('=', 1)[1] for line in versions.splitlines() if line.startswith('Fcitx5Core=')), '')
    if status == 'failed' and stages.get('probe') == 'failure' and core:
        reason = 'Fcitx5 ' + core + ' is below required 5.1.11'
    result = dict(distro=distro, arch=arch, status=status, reason=reason, fcitx5=core,
                  image=image, stages=stages, versions=versions)
    (directory / 'result.json').write_text(json.dumps(result, indent=2) + '\n')


def summary(root, output, selection='all'):
    rows = [json.loads(p.read_text()) for p in root.rglob('result.json')]
    indexed = {(r['distro'], r['arch']): r for r in rows}
    lines = ['# Linux platform trial results', '', '| Target | Architecture | Category | Result | Fcitx5 | Reason |',
             '|---|---|---|---|---|---|']
    for distro, arch, _, _, category in select_targets(selection):
        row = indexed.get((distro, arch), {})
        reason = row.get('reason', 'No result artifact; inspect job setup or runner logs').replace('|', '/')
        lines.append(f'| {distro} | {arch} | {category} | {row.get("status", "missing")} | {row.get("fcitx5", "")} | {reason} |')
    output.write_text('\n'.join(lines) + '\n')
    print(output.read_text())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    matrix_parser = sub.add_parser('matrix')
    matrix_parser.add_argument('--targets', default='all')
    check = sub.add_parser('inspect'); check.add_argument('image'); check.add_argument('platform'); check.add_argument('output', type=Path)
    report = sub.add_parser('record'); report.add_argument('directory', type=Path); report.add_argument('distro'); report.add_argument('arch'); report.add_argument('steps')
    collect = sub.add_parser('summary'); collect.add_argument('root', type=Path); collect.add_argument('output', type=Path)
    collect.add_argument('--targets', default='all')
    args = parser.parse_args()
    if args.command == 'matrix': print(json.dumps(matrix(args.targets)))
    elif args.command == 'inspect': inspect(args.image, args.platform, args.output)
    elif args.command == 'record': record(args.directory, args.distro, args.arch, args.steps)
    else: summary(args.root, args.output, args.targets)
