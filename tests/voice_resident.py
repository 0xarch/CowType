#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Explicit real-model residency check using synthetic PCM; never opens microphone.

Not auto-discovered: requires --binary and an existing supported external model.
"""
import argparse
import json
import os
from pathlib import Path
import selectors
import subprocess
import tempfile
import time

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary',type=Path,required=True)
parser.add_argument('--model-dir',type=Path)
args=parser.parse_args()
with tempfile.TemporaryDirectory(prefix='cowtype-resident-test-') as temporary:
    tools=Path(temporary)
    (tools/'parecord').symlink_to(Path(__file__).resolve().parent/'native/fake-parecord.py')
    env=os.environ|{'PATH':str(tools)+':'+os.environ['PATH']}
    if args.model_dir:
        env['COWTYPE_VOICE_MODEL_DIR']=str(args.model_dir.resolve())
    process=subprocess.Popen([str(args.binary.resolve()),'--serve'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env,bufsize=0)
    selector=selectors.DefaultSelector();selector.register(process.stdout,selectors.EVENT_READ)
    buffer=bytearray()
    def command(action,request):
        process.stdin.write(f'{action} {request}\n'.encode());process.stdin.flush()
    def reply(request,wanted,timeout=60):
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            if b'\n' not in buffer:
                if not selector.select(max(0,deadline-time.monotonic())):
                    break
                data=os.read(process.stdout.fileno(),65536)
                if not data:
                    raise RuntimeError('worker exited: '+process.stderr.read().decode(errors='replace'))
                buffer.extend(data)
            while b'\n' in buffer:
                line,_,rest=buffer.partition(b'\n');buffer[:]=rest
                request_text,kind,text=line.decode().split('\t',2)
                if int(request_text)!=request:
                    raise RuntimeError('cross-session message')
                if kind=='E': raise RuntimeError(text)
                if kind==wanted:return text
        raise TimeoutError(wanted)
    def stats():
        stat=Path(f'/proc/{process.pid}/stat').read_text().split()
        rss=int(stat[23])*os.sysconf('SC_PAGE_SIZE')//1024
        return int(stat[13])+int(stat[14]),rss
    try:
        prepare_start=time.monotonic();command('prepare',10);reply(10,'D')
        prepared_ms=round((time.monotonic()-prepare_start)*1000,2)
        assert Path(f'/proc/{process.pid}/task/{process.pid}/children').read_text().strip()==''
        readiness=[]
        for request in [1,2]:
            start=time.monotonic();command('start',request);reply(request,'R')
            readiness.append(round((time.monotonic()-start)*1000,2))
            assert reply(request,'V')=='0','synthetic silence must produce a zero level'
            time.sleep(0.75);command('stop',request)
            assert reply(request,'F')=='','synthetic silence must not yield text'
        # Cancel drops result, then a fresh request reuses the same model safely.
        command('start',3);reply(3,'R');command('cancel',3);reply(3,'D')
        command('start',4);reply(4,'R');time.sleep(0.15);command('stop',4);assert reply(4,'F')==''
        time.sleep(2)
        before,rss=stats();time.sleep(2);after,_=stats()
        # The helper must have released the synthetic recording child while idle.
        children=Path(f'/proc/{process.pid}/task/{process.pid}/children').read_text().strip()
        assert children=='',children
        ticks=after-before
        assert ticks<=2,'unexpected idle CPU consumption'
        print(json.dumps({'pid_reused':True,'prepare_ms':prepared_ms,'prepare_did_not_capture':True,'first_ready_ms':readiness[0],'second_ready_ms':readiness[1],
                          'idle_rss_kib':rss,'idle_cpu_ticks_over_2s':ticks,'microphone_opened':False,'sessions':4,'silence_level_verified':True}))
        process.stdin.close();process.wait(timeout=10)
        assert process.returncode==0
    finally:
        selector.close()
        if process.poll() is None:process.kill();process.wait()
