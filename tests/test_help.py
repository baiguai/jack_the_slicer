#!/usr/bin/env python3
"""End-to-end PTY test: the '?' help dialog lists the key bindings and closes."""
import os, pty, time, select, subprocess, sys

FAILURES = []

def check(name, cond):
    print(f"  [{'PASS' if cond else 'FAIL'}] {name}")
    if not cond:
        FAILURES.append(name)

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

os.write(master, b'?')
time.sleep(0.4)
buf_at_open = drain(1.0, 'Keyboard shortcuts')
check('help dialog opens on ?', b'Keyboard shortcuts' in buf_at_open)
for text in [b'Play the loaded .wav', b'Play the loaded .wav on loop',
             b'Open a .wav file', b'Stop playback', b'Close the dialog']:
    check(f'help lists: {text.decode()}', text in state['buf'])

os.write(master, b'\x1b')                # Esc closes
time.sleep(0.4)
drain(1.0)
check('Esc closes help and returns to main screen',
      b'Keyboard shortcuts' not in state['last'] and b'Ctrl+O' in state['last'])

os.write(master, b'?')                    # reopen, close with Enter
time.sleep(0.4)
drain(1.0, 'Keyboard shortcuts')
check('help reopens on ?', b'Keyboard shortcuts' in state['last'])
os.write(master, b'\r')
time.sleep(0.4)
drain(1.0)
check('Enter closes help', b'Keyboard shortcuts' not in state['last'])

os.write(master, b'q')
time.sleep(1.0)
rc = p.poll()
if rc is None:
    p.kill(); p.wait()
check('quit works after using help', rc is not None)
os.close(master)

if FAILURES:
    print(f"\nFAILED: {FAILURES}")
    sys.exit(1)
print("\nAll checks passed.")
sys.exit(0)