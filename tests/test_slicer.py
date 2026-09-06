#!/usr/bin/env python3
"""End-to-end PTY test: Ctrl+A slices the open .wav into /sliced files."""
import fcntl, glob, os, pty, re, shutil, struct, termios, time, select, subprocess, sys, filecmp

FAILURES = []
SLICED = '/tmp/jts_test/sliced'

def check(name, cond):
    print(f"  [{'PASS' if cond else 'FAIL'}] {name}")
    if not cond:
        FAILURES.append(name)

shutil.rmtree(SLICED, ignore_errors=True)

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

def loaded_path():
    s = re.sub(r'\x1b\[[0-9;]*m', '', state['buf'].decode('utf-8', 'replace'))
    idx = s.rfind('Loaded .wav:')
    if idx < 0:
        return None
    after = s[idx:].splitlines()
    for ln in after[1:]:
        t = ln.strip(' │')
        if t.startswith('/') or t == '(none)':
            return t
    return None

def slicer_files():
    return sorted(glob.glob(f'{SLICED}/*.wav'), key=os.path.getmtime)

# Load ramp.wav (mono 8 kHz, 8000 frames, divisible by 16 chunks).
os.write(master, b'\x0f')
drain(1.0, 'Files:')
time.sleep(0.2); os.write(master, b'\t'); time.sleep(0.3); drain(0.5)
os.write(master, b'\x1b[B'); time.sleep(0.3); drain(0.5)
os.write(master, b'\x1b[B'); time.sleep(0.3); drain(0.5)
os.write(master, b'\r'); time.sleep(0.8); drain(1.2, 'ramp.wav')
check('ramp.wav loaded', loaded_path() == '/tmp/jts_test/ramp.wav')
check('no slices before playing lengths set', b'Slices:' not in state['last'])
check('sliced dir still absent', not os.path.exists(SLICED))

# 2 bars, 1/8 slices -> 16 chunks.
os.write(master, b'\x1b[C'); time.sleep(0.3); drain(0.5)
os.write(master, b'\r'); time.sleep(0.4); drain(0.8)
os.write(master, b'\x1b[B'); time.sleep(0.3); drain(0.5)
for _ in range(3):
    os.write(master, b'\x1b[C'); time.sleep(0.25); drain(0.4)
os.write(master, b'\r'); time.sleep(0.5); drain(1.0)
check('16 slices configured', b'Slices: 16' in state['last'])

# Ctrl+A slices and stitches into a timestamped /sliced file.
os.write(master, b'\x01'); time.sleep(1.5); drain(1.5)
files = slicer_files()
check('one sliced wav created', len(files) == 1)
name_ok = re.match(r'ramp_\d{8}-\d{6}-\d{3}\.wav$',
                   os.path.basename(files[0])) is not None
check('sliced filename timestamped', name_ok)
check('slice is byte-identical (16x500 frames)',
      filecmp.cmp('/tmp/jts_test/ramp.wav', files[0], shallow=False))
check('app now points at the slice', loaded_path() == files[0])

# Another Ctrl+A accumulates a second, newer file.
os.write(master, b'\x01'); time.sleep(1.5); drain(1.5)
files = slicer_files()
check('second slice created', len(files) == 2)
check('two distinct files kept', files[0] != files[1])
check('app points at the newest slice', loaded_path() == files[-1])

# Ctrl+X reverts playback to the original without deleting slice files.
os.write(master, b'\x18'); time.sleep(0.8); drain(1.0)
check('Ctrl+X points back at original', loaded_path() == '/tmp/jts_test/ramp.wav')
check('slice files still on disk', len(slicer_files()) == 2)

# p/P still work after slicing round-trips (smoke check, no crash, quit works).
os.write(master, b'p'); time.sleep(0.6); drain(0.8)
os.write(master, b'P'); time.sleep(0.4); drain(0.5)
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