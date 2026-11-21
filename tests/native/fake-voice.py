#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generation-tagged resident fake. No audio or personal files."""
import os
from pathlib import Path
import sys
import time
root=Path(os.environ['COWTYPE_TEST_ROOT'])
active=None
for line in sys.stdin:
    action, value=line.split()
    request=int(value)
    if action=='prepare':
        (root/'voice-test-prepared').write_text(str(os.getpid()))
        print(f'{request}\tD\t',flush=True)
    elif action=='start':
        with (root/'voice-test-pids').open('a') as output:
            output.write(str(os.getpid())+'\n')
        if (root/'voice-test-error').exists():
            print(f'{request}\tE\t合成后端错误',flush=True)
            continue
        active=request
        # A deliberately late old result must never reach the next owner.
        print(f'{request-1}\tF\t过期结果',flush=True)
        print(f'{request}\tR\tready',flush=True)
        print(f'{request}\tP\t合成中间结果',flush=True)
        print(f'{request}\tV\t8',flush=True)
        print(f'{request}\tV\t2',flush=True)
    elif active==request and action=='stop':
        time.sleep(0.02)
        print(f'{request}\tF\t合成语音结果',flush=True)
        active=None
    elif active==request and action=='cancel':
        print(f'{request}\tF\t已取消的迟到结果',flush=True)
        active=None
