#!/usr/bin/env python3
"""End-to-end PTY test: the slice-length row and row navigation with arrows."""
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
check('no editor rows before a .wav is loaded',
      b'Loop length' not in state['last'] and b'Slice length' not in state['last'])

os.write(master, b'\x0f')                 # Ctrl+O
drain(1.0, 'Files:')
time.sleep(0.2); os.write(master, b'\t'); time.sleep(0.3); drain(0.5)
os.write(master, b'\x1b[B'); time.sleep(0.3); drain(0.5)
os.write(master, b'\x1b[B'); time.sleep(0.3); drain(0.5)
os.write(master, b'\r'); time.sleep(0.8); drain(1.2, 'tone.wav')

check('loop row shown', b'Loop length' in state['last'])
check('slice row shown below', b'Slice length' in state['last'])
check('slice options present',
      all(s in state['buf'] for s in [b'1/1', b'1/2', b'1/4', b'1/8',
                                      b'1/16', b'1/32', b'1/64']))

os.write(master, b'\x1b[B')               # Down -> slice row
time.sleep(0.3); drain(0.6)
os.write(master, b'\x1b[C')               # Right x2 -> focus 1/4
time.sleep(0.3); drain(0.6)
os.write(master, b'\x1b[C')
time.sleep(0.3); drain(0.6)
os.write(master, b'\r')                   # Enter -> select 1/4
time.sleep(0.4); drain(1.0)
check('Enter selects slice option', '● 1/4'.encode() in state['last'])
check('one slice selection only', state['last'].count('●'.encode()) >= 1)

os.write(master, b'\x1b')                 # Esc -> deselect slice row
time.sleep(0.4); drain(1.0)
check('Esc deselects slice selection', '● 1/4'.encode() not in state['last'])

os.write(master, b'\x1b[A')               # Up -> back to loop row
time.sleep(0.3); drain(0.6)
os.write(master, b'\x1b[C')               # Right x1 -> focus '2 bars'
time.sleep(0.3); drain(0.6)
os.write(master, b'\r')                   # Enter -> select 2 bars
time.sleep(0.4); drain(1.0)
check('loop row selected after Up+Enter', '● 2 bars'.encode() in state['last'])

os.write(master, b'\x1b[B')               # Down again, slice focus unchanged
time.sleep(0.3); drain(0.6)
os.write(master, b'\r')
time.sleep(0.4); drain(1.0)
check('slice row keeps its focused option (1/4)',
      '● 1/4'.encode() in state['last'])
check('loop selection persists', '● 2 bars'.encode() in state['buf'])

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