#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real-time synthetic silence source for explicitly invoked worker integration tests."""
import signal
import sys
import time
running=True
def stop(*_):
    global running
    running=False
signal.signal(signal.SIGINT,stop)
try:
    while running:
        sys.stdout.buffer.write(bytes(3200))
        sys.stdout.buffer.flush()
        time.sleep(0.1)
except BrokenPipeError:
    pass
