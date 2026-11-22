#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Refresh and verify the source inventory after reviewing local changes.
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
python3 "$script_dir/development.py" --refresh
exec python3 "$script_dir/development.py"
