#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate a release tag and extract the matching checked-in changelog section."""
import re
import sys
import tomllib
from pathlib import Path

root = Path(__file__).resolve().parents[1]
version = tomllib.loads((root / 'Cargo.toml').read_text())['package']['version']
if sys.argv[1:] != ['v' + version]:
    raise SystemExit('Tag must match the package version')
changelog = (root / 'CHANGELOG.md').read_text()
match = re.search(r'^## ' + re.escape(version) + r' —[^\n]*\n(.*?)(?=^## |\Z)', changelog, re.M | re.S)
if not match:
    raise SystemExit('Matching changelog section not found')
print(match.group(1).strip())
