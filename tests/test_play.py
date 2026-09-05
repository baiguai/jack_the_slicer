#!/usr/bin/env python3
"""End-to-end PTY test: loading a .wav then playing with P / looping with Shift+P.

Note: the app redraws only the cells that change, so 'latest frame' assertions
use the most recent chunk received from the pty; never the accumulated buffer
(which contains stale frames).
"""
import os, pty, time, select, subprocess, sys

FAILURES = []

def check(name, cond):
    print(f"  [{'PASS' if cond else 'FAIL'}] {name}")
    if not cond:
        FAILURES.append(name)

def spawn():
    master, slave = pty.openpty()
    p = subprocess.Popen(['./build/bin/App'], stdin=slave, stdout=slave,
                         stderr=slave, close_fds=True)
    os.close(slave)
    time.sleep(1.2)
    state = {'buf': b'', 'last': b''}
    def drain(t=1.2, stop=None):
        end = time.time() + t
        out = b''
        while time.time() < end:
            r, _, _ = select.select([master], [], [], 0.1)
            if r:
                try:
                    c = os.read(master, 65536)
                except OSError:
                    break
                if not c:
                    break
                state['buf'] += c
                out += c
                if stop and stop.encode() in out:
                    break
        state['last'] = out
        return state['buf']
    drain(1.0)
    return master, p, drain, state

def quit_app(master, p):
    os.write(master, b'q')
    time.sleep(1.0)
    rc = p.poll()
    if rc is None:
        p.kill(); p.wait()
    return rc is not None

def sink_inputs():
    return subprocess.run(['pactl', 'list', 'short', 'sink-inputs'],
                          capture_output=True, text=True).stdout.count('\n')

cfg = os.path.expanduser('~/.config/jack_the_slicer/config')
with open(cfg, 'w') as f:
    f.write('/tmp/jts_test')

print("Test: 'p' without a loaded file does nothing")
master, p, buf, state = spawn()
os.write(master, b'p')
time.sleep(0.4)
buf(0.6)
check('no Playing line without a loaded file', b'Playing:' not in state['last'])
check('still responsive (quit works)', quit_app(master, p))
os.close(master)
time.sleep(0.5)

print("Test: load a .wav, play with 'p', play looped with 'P'")
master, p, buf, state = spawn()
base_sinks = sink_inputs()
os.write(master, b'\x0f')                 # Ctrl+O
buf(1.0, 'Files:')
time.sleep(0.2)
os.write(master, b'\t')                   # Tab -> file list
time.sleep(0.3)
buf(0.5)
os.write(master, b'\x1b[B')               # ArrowDown -> foo.wav
time.sleep(0.3)
buf(0.5)
os.write(master, b'\x1b[B')               # ArrowDown -> tone.wav
time.sleep(0.3)
buf(0.5)
os.write(master, b'\r')                   # Enter -> load tone.wav
time.sleep(0.8)
buf(1.2, 'tone.wav')
check('loaded tone.wav shown', b'Loaded .wav:' in state['buf'])

os.write(master, b'p')                    # play once
time.sleep(0.6)
buf(1.0)
check("'p' starts non-looped playback",
      b'Playing:' in state['last'] and b'(looped)' not in state['last'])
check('audio sink active while playing', sink_inputs() > base_sinks)

os.write(master, b'P')                    # restart looped
time.sleep(0.7)
buf(1.0, '(looped)')
check("'P' shows looped playback", b'(looped)' in state['last'])
check('audio sink active while looping', sink_inputs() > base_sinks)

os.write(master, b'\x1b')                 # Escape while looping -> stop
time.sleep(0.5)
buf(0.8)
check('Escape stops playback', b'Playing:' not in state['last'])
check('audio sink released after Escape', sink_inputs() <= base_sinks)

os.write(master, b'p')                    # back to single pass
time.sleep(0.6)
buf(1.0)
check("'p' restarts non-looped from looped",
      b'Playing:' in state['last'] and b'(looped)' not in state['last'])

time.sleep(3.5)                           # tone.wav is 1s -> let it finish
buf(1.0)
check('non-loop playback stops when done', b'Playing:' not in state['last'])

os.write(master, b'p')                    # file stays usable -> replay
time.sleep(0.6)
buf(1.0)
check('file still loaded and playable',
      b'Playing:' in state['last'] and b'(looped)' not in state['last'])

check('quit while playing is clean', quit_app(master, p))
os.close(master)

if FAILURES:
    print(f"\nFAILED: {FAILURES}")
    sys.exit(1)
print("\nAll checks passed.")
sys.exit(0)