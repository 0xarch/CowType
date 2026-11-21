#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the native key names from the shared feature shortcut registry."""
import json
import sys
from pathlib import Path

rows = [line.split('\t') for line in Path(sys.argv[1]).read_text().splitlines()]
choices = rows[0][2].split(',')
if len(choices) != len(set(choices)) or any(row[1] not in choices or row[2].split(',') != choices for row in rows):
    raise SystemExit('All shortcut actions must share unique choices and valid defaults')
Path(sys.argv[2]).write_text('// Generated from src/config/shortcuts.tsv.\n#pragma once\n#include <array>\n#include <string_view>\n'
    + 'inline constexpr std::array<std::string_view, ' + str(len(choices)) + '> ctShortcutNames{'
    + ','.join(json.dumps(key) for key in choices) + '};\n')
