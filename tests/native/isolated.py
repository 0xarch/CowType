#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the independent probes with both legacy and current Fcitx paths isolated."""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--root', type=Path, required=True)
parser.add_argument('--prefix', type=Path, required=True)
parser.add_argument('--data', type=Path, required=True)
parser.add_argument('command', nargs=argparse.REMAINDER)
args = parser.parse_args()
root = args.root.resolve()
prefix = args.prefix.resolve()
if not prefix.is_relative_to(root):
    parser.error('prefix must be inside test root')
env = os.environ.copy()
for name, path in {
    'XDG_CONFIG_HOME': root / 'xdg/config', 'XDG_CONFIG_DIRS': root / 'xdg/config',
    'XDG_DATA_HOME': root / 'xdg/data', 'XDG_DATA_DIRS': prefix / 'share',
    'XDG_CACHE_HOME': root / 'xdg/cache', 'XDG_STATE_HOME': root / 'xdg/state', 'XDG_RUNTIME_DIR': root / 'xdg/runtime',
    'FCITX_CONFIG_HOME': root / 'xdg/config/fcitx5', 'FCITX_CONFIG_DIRS': root / 'xdg/config/fcitx5',
    'FCITX_DATA_HOME': prefix / 'share/fcitx5', 'FCITX_DATA_DIRS': prefix / 'share/fcitx5',
    'FCITX_ADDON_DIRS': prefix / 'lib/fcitx5',
}.items():
    path.mkdir(parents=True, exist_ok=True)
    env[name] = str(path)
env.update(COWTYPE_SYSTEM_PINYIN_DICT='', COWTYPE_STATISTICAL_MODEL_FILE='', COWTYPE_TEST_ROOT=str(root), COWTYPE_DATA_DIR=str(args.data.resolve()),
           SKIP_FCITX_PATH='1', SKIP_FCITX_SYSTEM_PATH='0', SKIP_FCITX_USER_PATH='0',
           DISPLAY='', WAYLAND_DISPLAY='')
raise SystemExit(subprocess.run(args.command, env=env).returncode)
