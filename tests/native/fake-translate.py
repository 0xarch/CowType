#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthetic private-pipe translation; never reads credentials or contacts a service."""
import sys
import time
line=sys.stdin.buffer.readline(65538)
try:
    text=bytes.fromhex(line.decode().strip()).decode()
    time.sleep(0.12)
    if text not in ('山','selected text','hello worl','failure','工'):
        print('E\ttranslate.error.input')
    elif text=='failure':
        print('E\ttranslate.error.rate')
    else:
        print('F\t'+'合成译文'.encode().hex())
except Exception:
    print('E\ttranslate.error.response')
