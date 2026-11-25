# SPDX-License-Identifier: GPL-3.0-or-later
"""Stage the installed Lua runtime under /tmp; never start a desktop frontend."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix='cowtype-lua-') as temporary:
    root = Path(temporary)
    prefix = root / 'prefix'
    addon = prefix / 'share/fcitx5/addon'
    lua = prefix / 'share/fcitx5/lua/imeapi'
    libs = prefix / 'lib/fcitx5'
    addon.mkdir(parents=True); lua.mkdir(parents=True); libs.mkdir(parents=True)
    for name in ('imeapi', 'luaaddonloader'):
        shutil.copyfile(Path(sys.argv[2]) / ('addon/' + name + '.conf'), addon / (name + '.conf'))
    shutil.copyfile(Path(sys.argv[2]) / 'lua/imeapi/imeapi.lua', lua / 'imeapi.lua')
    extension = lua / 'extensions'
    extension.mkdir()
    shutil.copyfile(Path(__file__).resolve().parents[2] / 'src-scripts/cowtype.lua', extension / 'cowtype.lua')
    shutil.copyfile(sys.argv[3], libs / 'libluaaddonloader.so')
    fixtures = Path(__file__).resolve().parents[1] / 'fixtures'
    for name, source in (('ice', 'basic.tsv'), ('wubi', 'wubi.tsv')):
        target = root / 'data' / name
        target.mkdir(parents=True)
        shutil.copyfile(fixtures / source, target / 'lexicon.tsv')
    result = subprocess.run([sys.executable, str(Path(__file__).with_name('isolated.py')),
                             '--root', str(root), '--prefix', str(prefix), '--data', str(root / 'data'), sys.argv[1], sys.argv[4]], timeout=15)
    raise SystemExit(result.returncode)
