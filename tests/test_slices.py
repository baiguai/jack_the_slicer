#!/usr/bin/env python3
"""End-to-end PTY test: the slice column (row 3) with per-slice effects."""
import fcntl, os, pty, termios, time, select, struct, subprocess, sys

FAILURES = []

def check(name, cond):
    print(f"  [{'PASS' if cond else 'FAIL'}] {name}")
    if not cond:
        FAILURES.append(name)

cfg = os.path.expanduser('~/.config/jack_the_slicer/config')
with open(cfg, 'w') as f:
    f.write('/tmp/jts_test')

master, slave = pty.openpty()
fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack('HHHH', 40, 80, 0, 0))
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

# Load tone.wav via the file dialog.
os.write(master, b'\x0f')
drain(1.0, 'Files:')
time.sleep(0.2); os.write(master, b'\t'); time.sleep(0.3); drain(0.5)
os.write(master, b'\x1b[B'); time.sleep(0.3); drain(0.5)
os.write(master, b'\x1b[B'); time.sleep(0.3); drain(0.5)
os.write(master, b'\r'); time.sleep(0.8); drain(1.2, 'tone.wav')
check('row 3 hidden before lengths are set', b'Slices:' not in state['last'])

# Loop length -> 2 bars.
os.write(master, b'\x1b[C'); time.sleep(0.3); drain(0.5)
os.write(master, b'\r'); time.sleep(0.4); drain(0.8)
# Slice length -> 1/8 (3 rights from 1/1).
os.write(master, b'\x1b[B'); time.sleep(0.3); drain(0.5)
for _ in range(3):
    os.write(master, b'\x1b[C'); time.sleep(0.3); drain(0.5)
os.write(master, b'\r'); time.sleep(0.4); drain(1.2)
check('row 3 appears once both lengths are set', b'Slices:' in state['last'])
check('count is bars * slices', b'Slices: 16' in state['last'])
check('breakdown shown', '×'.encode('utf-8') in state['last'])
check('first slice listed', b'     1  Shuffle' in state['last'])
check('all 16 slices visible without scrolling', b'    16  Shuffle' in state['last'])
check('no scroll markers on a tall terminal', '⋮'.encode('utf-8') not in state['last'])

# Down -> slice column, Down -> slice 2, Enter -> open effect menu.
os.write(master, b'\x1b[B'); time.sleep(0.3); drain(0.5)
os.write(master, b'\x1b[B'); time.sleep(0.3); drain(0.5)
os.write(master, b'\r'); time.sleep(0.4); drain(1.0)
check('effect menu opens on Enter', b'Shuffle' in state['last'])
for effect in [b'None', b'Shuffle', b'Reverse', b'Stretch', b'Squish']:
    check(f'effect listed: {effect.decode()}', effect in state['last'])
check('Shuffle is highlighted first', b'> Shuffle' in state['last'])
check('slice still defaults to Shuffle', b'     2  Shuffle' in state['last'])

# Esc cancels the menu without applying.
os.write(master, b'\x1b'); time.sleep(0.4); drain(1.0)
check('Esc closes the menu without applying', b'> Shuffle' not in state['last'])
check('slice unchanged after cancel', b'     2  Shuffle' in state['last'])

# Reopen, choose Reverse (move down twice), apply.
os.write(master, b'\r'); time.sleep(0.4); drain(1.0)
os.write(master, b'\x1b[B'); time.sleep(0.3); drain(0.6)   # -> Reverse
os.write(master, b'\x1b[B'); time.sleep(0.3); drain(0.6)   # -> Stretch
os.write(master, b'\x1b[A'); time.sleep(0.3); drain(0.6)   # back up to Reverse
os.write(master, b'\r'); time.sleep(0.4); drain(1.0)
check('Enter applies the highlighted effect', b'     2  Reverse' in state['last'])
check('no menu remnant', b'> Reverse' not in state['last'])

# Esc clears the effect on the focused slice.
os.write(master, b'\x1b'); time.sleep(0.4); drain(1.0)
check('Esc clears a slice with a selection', b'     2  Shuffle' not in state['last'])
check('slice back to None', b'     2  None' in state['last'])

# Shrink to 24 rows: list must scroll, showing markers and the tail.
fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 80, 0, 0))
os.write(master, b'\x1b[A'); time.sleep(0.3); drain(0.6)   # up to slice row
os.write(master, b'\x1b[B'); time.sleep(0.3); drain(0.6)   # back into column
def focused_slice():
    s = state['last'].decode('utf-8', 'replace')
    for l in s.splitlines():
        if '\x1b[1m\x1b[36m' in l and 'Shuffle' in l:
            toks = l.split()
            for i, t in enumerate(toks):
                if t == 'Shuffle':
                    return toks[i-1] if i > 0 else None
    return None
for _ in range(20):
    os.write(master, b'\x1b[B'); time.sleep(0.2); drain(0.4)
    if focused_slice() == '16':
        break
check('scrolled to last slice', focused_slice() == '16')
check('scroll marker shown', '⋮'.encode('utf-8') in state['last'])
check('top rows hidden while scrolled', b'     5  Shuffle' not in state['last'])
for _ in range(20):
    os.write(master, b'\x1b[A'); time.sleep(0.2); drain(0.4)
    if focused_slice() == '1':
        break
check('back to first slice', focused_slice() == '1')
check('top rows visible again at top', b'     6  Shuffle' in state['last'])

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