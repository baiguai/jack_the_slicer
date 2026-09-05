#!/usr/bin/env python3
"""End-to-end PTY test of the "open .wav" dialog (typing + Tab completion)."""
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
                if stop and stop.encode() in state['buf']:
                    break
        state['last'] = out
        return state['buf']
    drain(1.0)
    return master, p, drain

def quit_app(master, p):
    os.write(master, b'q')
    time.sleep(1.0)
    rc = p.poll()
    if rc is None:
        p.kill(); p.wait()
    return rc is not None

cfg = os.path.expanduser('~/.config/jack_the_slicer/config')
os.makedirs(os.path.dirname(cfg), exist_ok=True)
with open(cfg, 'w') as f:                 # open the dialog in the test dir
    f.write('/tmp/jts_test')

os.makedirs('/tmp/jts_test', exist_ok=True)
with open('/tmp/jts_test/foo.wav', 'wb') as f:
    f.write(b'RIFF' + b'\x00' * 100)

print("Test: type partial path + Tab + Enter reaches load, modal closes, config saved")
master, p, buf = spawn()
os.write(master, b'\x0f')                 # Ctrl+O
buf()
time.sleep(0.3)
os.write(master, b'fo')                   # append to the kept full path (/tmp/jts_test/fo)
time.sleep(0.4)
buf(0.8)
os.write(master, b'\t')                   # Tab -> completes to foo.wav, cursor to end
time.sleep(0.4)
buf(0.8)
os.write(master, b'X')                    # cursor is at end: appends, not mid-insert
time.sleep(0.4)
b = buf(0.8)
check('Tab puts cursor at end (typed char appends)', b'/tmp/jts_test/foo.wavX' in b)
os.write(master, b'\x7f')                 # Backspace -> remove the X
time.sleep(0.4)
b = buf(0.8)
check('backspace removes from end', b'foo.wavX' not in b.split(b'\n').pop())
os.write(master, b'\r')                   # Enter -> load
time.sleep(0.8)
buf(1.2, 'Loaded')
base = buf()
check('modal opens on Ctrl+O', b'Path:' in base)
check('path bar kept current dir on open', b'/tmp/jts_test/' in base)
check('loaded path shown (full path kept while typing)',
      b'/tmp/jts_test/foo.wav' in base)
check('q exits after load (modal closed)', quit_app(master, p))
os.close(master)
check('parent dir saved to config',
      os.path.exists(cfg) and open(cfg).read().strip() == '/tmp/jts_test')
time.sleep(0.5)

print("Test: arrow-key navigation + Enter loads (no typing)")
master, p, buf = spawn()
os.write(master, b'\x0f')                 # Ctrl+O opens in /tmp/jts_test
buf(1.0, 'Files:')
time.sleep(0.2)
os.write(master, b'\t')                   # Tab -> focus file list
time.sleep(0.3)
buf(0.6)
os.write(master, b'\x1b[B')               # ArrowDown -> foo.wav
time.sleep(0.3)
buf(0.6)
os.write(master, b'\r')                   # Enter -> open foo.wav
time.sleep(0.8)
buf(1.2, 'Loaded')
b = buf()
check('menu navigation loads file', b'/tmp/jts_test/foo.wav' in b)
check('q exits after menu load', quit_app(master, p))
os.close(master)

if FAILURES:
    print(f"\nFAILED: {FAILURES}")
    sys.exit(1)
print("\nAll checks passed.")
sys.exit(0)