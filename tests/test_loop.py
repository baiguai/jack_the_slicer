#!/usr/bin/env python3
"""End-to-end PTY test: the loop-length selector row (1-4 bars of 4/4)."""
import os, pty, time, select, subprocess, sys

FAILURES = []

def check(name, cond):
    print(f"  [{'PASS' if cond else 'FAIL'}] {name}")
    if not cond:
        FAILURES.append(name)

cfg = os.path.expanduser('~/.config/jack_the_slicer/config')
with open(cfg, 'w') as f:
    f.write('/tmp/jts_test')

master, slave = pty.openpty()
p = subprocess.Popen(['./build/bin/App'], stdin=slave, stdout=slave,
                     stderr=slave, close_fds=True)
os.close(slave)
time.sleep(1.2)
state = {'buf': b'', 'last': b''}

def drain(t=1.0, stop=None):
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

drain(0.8)
check('selector hidden before a .wav is loaded', b'Loop length' not in state['last'])

os.write(master, b'\x0f')                 # Ctrl+O
drain(1.0, 'Files:')
time.sleep(0.2)
os.write(master, b'\t')
time.sleep(0.3)
drain(0.5)
os.write(master, b'\x1b[B'); time.sleep(0.3); drain(0.5)   # foo.wav
os.write(master, b'\x1b[B'); time.sleep(0.3); drain(0.5)   # tone.wav
os.write(master, b'\r')
time.sleep(0.8)
drain(1.2, 'tone.wav')
check('selector shown once a .wav is loaded', b'Loop length' in state['last'])
check('no selection initially', '●'.encode() not in state['last'])
check('four options present',
      all(s in state['buf'] for s in [b'1 bar', b'2 bars', b'3 bars', b'4 bars']))

os.write(master, b'\x1b[C')               # ArrowRight x2 -> focus '3 bars'
time.sleep(0.3); drain(0.6)
os.write(master, b'\x1b[C')
time.sleep(0.3); drain(0.6)
os.write(master, b'\r')                   # Enter -> select 3 bars
time.sleep(0.4); drain(1.0)
check('Enter selects the focused option', '● 3 bars'.encode() in state['last'])
check('exactly one option selected', state['last'].count('●'.encode()) == 1)

os.write(master, b'\x1b[D')               # ArrowLeft -> focus '2 bars'
time.sleep(0.3); drain(0.6)
os.write(master, b'\r')                   # Enter -> reselect
time.sleep(0.4); drain(1.0)
check('selection moves to 2 bars', '● 2 bars'.encode() in state['last'])
check('previous selection cleared', '● 3 bars'.encode() not in state['last'])
check('still exactly one selected', state['last'].count('●'.encode()) == 1)

os.write(master, b'\x1b')                 # Esc -> deselect
time.sleep(0.4); drain(1.0)
check('Esc deselects the loop length', '●'.encode() not in state['last'])
check('selector row still visible after deselect', b'Loop length' in state['last'])

os.write(master, b'q')
time.sleep(1.0)
rc = p.poll()
if rc is None:
    p.kill(); p.wait()
check('quit works', rc is not None)
os.close(master)

if FAILURES:
    print(f"\nFAILED: {FAILURES}")
    sys.exit(1)
print("\nAll checks passed.")
sys.exit(0)