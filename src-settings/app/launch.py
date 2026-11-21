#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Keep settings startup diagnostics available to desktop-launcher users."""
import datetime
import os
from pathlib import Path
import runpy
import sys
import traceback


def main():
    state = Path(os.environ.get('XDG_STATE_HOME') or Path.home() / '.local/state') / 'cowtype'
    try:
        state.mkdir(parents=True, mode=0o700, exist_ok=True)
        path = state / 'settings.log'
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_APPEND | os.O_NOFOLLOW, 0o600)
        os.fchmod(fd, 0o600)
        if os.fstat(fd).st_size > 1024 * 1024:
            os.ftruncate(fd, 0)
        os.dup2(fd, sys.stderr.fileno())
        os.close(fd)
        print(f'{datetime.datetime.now(datetime.timezone.utc).isoformat()} Settings starting', file=sys.stderr, flush=True)
    except OSError:
        print('Could not open the settings diagnostic log; using standard error', file=sys.stderr)
    try:
        runpy.run_path(str(Path(__file__).with_name('main.py')), run_name='__main__')
    except SystemExit as error:
        return error.code
    except Exception as error:
        # Preserve locations and exception type, never config contents or model data.
        traceback.print_tb(error.__traceback__, file=sys.stderr)
        print(f'Settings startup failed: {type(error).__name__}', file=sys.stderr, flush=True)
        return 1


if __name__ == '__main__':
    sys.exit(main())
