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

# Ctrl+A applies even with no row settings: whole-file 1-chunk copy.
os.write(master, b'\x01'); time.sleep(1.5); drain(1.5)
files = slicer_files()
check('Ctrl+A works before rows are configured', len(files) == 1)
name_ok = re.match(r'ramp_\d{8}-\d{6}-\d{3}\.wav$',
                   os.path.basename(files[0])) is not None
check('sliced filename timestamped', name_ok)
check('slice is byte-identical (1-chunk whole file)',
      filecmp.cmp('/tmp/jts_test/ramp.wav', files[0], shallow=False))
check('app now points at the slice', loaded_path() == files[0])

# 2 bars, 1/8 slices -> 16 chunks.
os.write(master, b'\x1b[C'); time.sleep(0.3); drain(0.5)
os.write(master, b'\r'); time.sleep(0.4); drain(0.8)
os.write(master, b'\x1b[B'); time.sleep(0.3); drain(0.5)
for _ in range(3):
    os.write(master, b'\x1b[C'); time.sleep(0.25); drain(0.4)
os.write(master, b'\r'); time.sleep(0.5); drain(1.0)
check('16 slices configured', b'Slices: 16' in state['last'])

def is_shuffled_arrangement(out_path, src_path='/tmp/jts_test/ramp.wav'):
    with open(src_path, 'rb') as f:
        src = f.read()
    with open(out_path, 'rb') as f:
        out = f.read()
    pcm_src, pcm_out = src[44:], out[44:]
    if len(pcm_src) != len(pcm_out) or len(pcm_out) % 1000 != 0:
        return False
    orig = [pcm_src[i:i + 1000] for i in range(0, len(pcm_src), 1000)]
    return all(pcm_out[i:i + 1000] in orig for i in range(0, len(pcm_out), 1000))

# Ctrl+A stitches 16 equal chunks into a second, newer file.
os.write(master, b'\x01'); time.sleep(1.5); drain(1.5)
files = slicer_files()
check('second slice created', len(files) == 2)
check('slice is a shuffle of the 16 chunks',
      is_shuffled_arrangement(files[-1]))
check('all-Shuffle default really reordered',
      not filecmp.cmp('/tmp/jts_test/ramp.wav', files[-1], shallow=False))
check('app points at the newest slice', loaded_path() == files[-1])

# Ctrl+A again, rows still unchanged, keeps accumulating.
os.write(master, b'\x01'); time.sleep(1.5); drain(1.5)
files = slicer_files()
check('third slice created with rows unchanged', len(files) == 3)
check('files kept distinct', len(set(files)) == 3)
check('third slice also a shuffle arrangement',
      is_shuffled_arrangement(files[-1]))
check('app points at the newest slice', loaded_path() == files[-1])

# Play the newest slice on loop, then Esc: stops without clearing any rows.
os.write(master, b'P'); time.sleep(0.6); drain(0.8)
check('slice is being played', b'Playing:' in state['last'])
os.write(master, b'\x1b'); time.sleep(0.8); drain(1.0)
check('Esc stops playback', b'Playing:' not in state['last'])
check('bars row not cleared by Esc', '● 2 bars'.encode() in state['last'])
check('slice row not cleared by Esc', '● 1/8'.encode() in state['last'])
check('slice column not cleared by Esc', b'Slices: 16' in state['last'])

# Ctrl+X reverts playback to the original without deleting slice files.
os.write(master, b'\x18'); time.sleep(0.8); drain(1.0)
check('Ctrl+X points back at original',
      loaded_path() == '/tmp/jts_test/ramp.wav')
check('slice files still on disk', len(slicer_files()) == 3)

os.write(master, b'q')
time.sleep(1.0)
rc = p.poll()
if rc is None:
    p.kill(); p.wait()
check('quit works', rc is not None)
os.close(master)

# --- Second session: Reverse is applied per-slice at the frame level. ---
existing = len(slicer_files())
master2, slave2 = pty.openpty()
fcntl.ioctl(master2, termios.TIOCSWINSZ, struct.pack('HHHH', 40, 80, 0, 0))
p2 = subprocess.Popen(['./build/bin/App'], stdin=slave2, stdout=slave2,
                      stderr=slave2, close_fds=True)
os.close(slave2)
time.sleep(1.2)
state2 = {'buf': b'', 'last': b''}

def drain2(t=1.0, stop=None):
    end = time.time() + t
    out = b''
    while time.time() < end:
        r, _, _ = select.select([master2], [], [], 0.1)
        if r:
            try:
                c = os.read(master2, 65536)
            except OSError:
                break
            if not c:
                break
            state2['buf'] += c
            out += c
            if stop and stop.encode() in out:
                break
    state2['last'] = out
    return state2['buf']

drain2(0.8)

# Load ramp.wav (the sliced/ dir from session 1 shifts the listing: skip it).
os.write(master2, b'\x0f'); drain2(1.0, 'Files:')
time.sleep(0.2); os.write(master2, b'\t'); time.sleep(0.3); drain2(0.5)
os.write(master2, b'\x1b[B'); time.sleep(0.3); drain2(0.5)   # sliced/
os.write(master2, b'\x1b[B'); time.sleep(0.3); drain2(0.5)   # foo.wav
os.write(master2, b'\x1b[B'); time.sleep(0.3); drain2(0.5)   # ramp.wav
os.write(master2, b'\r'); time.sleep(0.8); drain2(1.2)

# 1 bar, 1/2 slices -> 2 slices.
os.write(master2, b'\r'); time.sleep(0.4); drain2(0.8)     # bars: select 1 bar
os.write(master2, b'\x1b[B'); time.sleep(0.3); drain2(0.5)  # down -> slice length
os.write(master2, b'\x1b[C'); time.sleep(0.3); drain2(0.5)  # right -> 1/2
os.write(master2, b'\r'); time.sleep(0.5); drain2(1.0)
check('2 slices configured (reverse session)', b'Slices: 2' in state2['last'])

# Down into the slice column; slice 1 -> Reverse, slice 2 -> None.
os.write(master2, b'\x1b[B'); time.sleep(0.3); drain2(0.6)
os.write(master2, b'\r'); time.sleep(0.4); drain2(1.0)     # open effect menu
os.write(master2, b'\x1b[B'); time.sleep(0.3); drain2(0.6)  # Shuffle -> Reverse
os.write(master2, b'\r'); time.sleep(0.4); drain2(1.0)
check('slice 1 set to Reverse', b'     1  Reverse' in state2['last'])
os.write(master2, b'\x1b[B'); time.sleep(0.3); drain2(0.6)  # down -> slice 2
os.write(master2, b'\r'); time.sleep(0.4); drain2(1.0)     # open effect menu
for _ in range(5):
    os.write(master2, b'\x1b[B'); time.sleep(0.2); drain2(0.4)  # wrap to None
os.write(master2, b'\r'); time.sleep(0.4); drain2(1.0)
check('slice 2 set to None', b'     2  None' in state2['last'])

# Apply and verify chunk 1 is reversed, chunk 2 untouched.
os.write(master2, b'\x01'); time.sleep(1.5); drain2(1.5)
files = slicer_files()
check('reverse-session slice created', len(files) == existing + 1)
src_pcm = open('/tmp/jts_test/ramp.wav', 'rb').read()[44:]
out_pcm = open(files[-1], 'rb').read()[44:]
c0, c1 = src_pcm[:8000], src_pcm[8000:16000]
rev0 = b''.join(c0[i:i + 2] for i in range(len(c0) - 2, -1, -2))
check('first chunk reversed frame-by-frame', out_pcm[:8000] == rev0)
check('second chunk kept in original order', out_pcm[8000:16000] == c1)
check('output differs from source', out_pcm != src_pcm)

os.write(master2, b'q')
time.sleep(1.0)
rc2 = p2.poll()
if rc2 is None:
    p2.kill(); p2.wait()
check('reverse session quits', rc2 is not None)
os.close(master2)

# --- Third session: Stretch plays the first half of a slice, each frame
# --- held twice, filling the full chunk duration. ---
existing = len(slicer_files())
master3, slave3 = pty.openpty()
fcntl.ioctl(master3, termios.TIOCSWINSZ, struct.pack('HHHH', 40, 80, 0, 0))
p3 = subprocess.Popen(['./build/bin/App'], stdin=slave3, stdout=slave3,
                      stderr=slave3, close_fds=True)
os.close(slave3)
time.sleep(1.2)
state3 = {'buf': b'', 'last': b''}

def drain3(t=1.0, stop=None):
    end = time.time() + t
    out = b''
    while time.time() < end:
        r, _, _ = select.select([master3], [], [], 0.1)
        if r:
            try:
                c = os.read(master3, 65536)
            except OSError:
                break
            if not c:
                break
            state3['buf'] += c
            out += c
            if stop and stop.encode() in out:
                break
    state3['last'] = out
    return state3['buf']

drain3(0.8)

# Load ramp.wav (sliced/ still shifts the listing).
os.write(master3, b'\x0f'); drain3(1.0, 'Files:')
time.sleep(0.2); os.write(master3, b'\t'); time.sleep(0.3); drain3(0.5)
os.write(master3, b'\x1b[B'); time.sleep(0.3); drain3(0.5)
os.write(master3, b'\x1b[B'); time.sleep(0.3); drain3(0.5)
os.write(master3, b'\x1b[B'); time.sleep(0.3); drain3(0.5)
os.write(master3, b'\r'); time.sleep(0.8); drain3(1.2)

# 1 bar, 1/2 slices -> 2 slices.
os.write(master3, b'\r'); time.sleep(0.4); drain3(0.8)
os.write(master3, b'\x1b[B'); time.sleep(0.3); drain3(0.5)
os.write(master3, b'\x1b[C'); time.sleep(0.3); drain3(0.5)
os.write(master3, b'\r'); time.sleep(0.5); drain3(1.0)
check('2 slices configured (stretch session)', b'Slices: 2' in state3['last'])

# Slice 1 -> Stretch, slice 2 -> None.
os.write(master3, b'\x1b[B'); time.sleep(0.3); drain3(0.6)
os.write(master3, b'\r'); time.sleep(0.4); drain3(1.0)     # open effect menu
os.write(master3, b'\x1b[B'); time.sleep(0.3); drain3(0.6)  # Shuffle -> Reverse
os.write(master3, b'\x1b[B'); time.sleep(0.3); drain3(0.6)  # -> Stretch
os.write(master3, b'\r'); time.sleep(0.4); drain3(1.0)
check('slice 1 set to Stretch', b'     1  Stretch' in state3['last'])
os.write(master3, b'\x1b[B'); time.sleep(0.3); drain3(0.6)  # down -> slice 2
os.write(master3, b'\r'); time.sleep(0.4); drain3(1.0)     # open effect menu
for _ in range(5):
    os.write(master3, b'\x1b[B'); time.sleep(0.2); drain3(0.4)  # wrap to None
os.write(master3, b'\r'); time.sleep(0.4); drain3(1.0)
check('slice 2 set to None', b'     2  None' in state3['last'])

# Apply and verify chunk 1 is the first half of the source, each frame twice.
os.write(master3, b'\x01'); time.sleep(1.5); drain3(1.5)
files = slicer_files()
check('stretch-session slice created', len(files) == existing + 1)
out_pcm = open(files[-1], 'rb').read()[44:]
st0 = b''
i = 0
while i < 4000:                      # first 2000 frames = 4000 bytes of c0
    frame = c0[i:i + 2]
    st0 += frame + frame
    i += 2
check('first chunk is half-length stretched', out_pcm[:8000] == st0)
check('second chunk kept in original order', out_pcm[8000:16000] == c1)
check('output differs from source', out_pcm != src_pcm)

os.write(master3, b'q')
time.sleep(1.0)
rc3 = p3.poll()
if rc3 is None:
    p3.kill(); p3.wait()
check('stretch session quits', rc3 is not None)
os.close(master3)

# --- Fourth session: Squish drops every other frame, then plays the kept
# --- frames twice to keep the chunk length. ---
existing = len(slicer_files())
master4, slave4 = pty.openpty()
fcntl.ioctl(master4, termios.TIOCSWINSZ, struct.pack('HHHH', 40, 80, 0, 0))
p4 = subprocess.Popen(['./build/bin/App'], stdin=slave4, stdout=slave4,
                      stderr=slave4, close_fds=True)
os.close(slave4)
time.sleep(1.2)
state4 = {'buf': b'', 'last': b''}

def drain4(t=1.0, stop=None):
    end = time.time() + t
    out = b''
    while time.time() < end:
        r, _, _ = select.select([master4], [], [], 0.1)
        if r:
            try:
                c = os.read(master4, 65536)
            except OSError:
                break
            if not c:
                break
            state4['buf'] += c
            out += c
            if stop and stop.encode() in out:
                break
    state4['last'] = out
    return state4['buf']

drain4(0.8)

# Load ramp.wav (sliced/ still shifts the listing).
os.write(master4, b'\x0f'); drain4(1.0, 'Files:')
time.sleep(0.2); os.write(master4, b'\t'); time.sleep(0.3); drain4(0.5)
os.write(master4, b'\x1b[B'); time.sleep(0.3); drain4(0.5)
os.write(master4, b'\x1b[B'); time.sleep(0.3); drain4(0.5)
os.write(master4, b'\x1b[B'); time.sleep(0.3); drain4(0.5)
os.write(master4, b'\r'); time.sleep(0.8); drain4(1.2)

# 1 bar, 1/2 slices -> 2 slices.
os.write(master4, b'\r'); time.sleep(0.4); drain4(0.8)
os.write(master4, b'\x1b[B'); time.sleep(0.3); drain4(0.5)
os.write(master4, b'\x1b[C'); time.sleep(0.3); drain4(0.5)
os.write(master4, b'\r'); time.sleep(0.5); drain4(1.0)
check('2 slices configured (squish session)', b'Slices: 2' in state4['last'])

# Slice 1 -> Squish, slice 2 -> None.
os.write(master4, b'\x1b[B'); time.sleep(0.3); drain4(0.6)
os.write(master4, b'\r'); time.sleep(0.4); drain4(1.0)     # open effect menu
os.write(master4, b'\x1b[B'); time.sleep(0.3); drain4(0.6)  # Shuffle -> Reverse
os.write(master4, b'\x1b[B'); time.sleep(0.3); drain4(0.6)  # -> Stretch
os.write(master4, b'\x1b[B'); time.sleep(0.3); drain4(0.6)  # -> Squish
os.write(master4, b'\r'); time.sleep(0.4); drain4(1.0)
check('slice 1 set to Squish', b'     1  Squish' in state4['last'])
os.write(master4, b'\x1b[B'); time.sleep(0.3); drain4(0.6)  # down -> slice 2
os.write(master4, b'\r'); time.sleep(0.4); drain4(1.0)     # open effect menu
for _ in range(5):
    os.write(master4, b'\x1b[B'); time.sleep(0.2); drain4(0.4)  # wrap to None
os.write(master4, b'\r'); time.sleep(0.4); drain4(1.0)
check('slice 2 set to None', b'     2  None' in state4['last'])

# Apply and verify chunk 1 is the even frames of the source, twice.
os.write(master4, b'\x01'); time.sleep(1.5); drain4(1.5)
files = slicer_files()
check('squish-session slice created', len(files) == existing + 1)
out_pcm = open(files[-1], 'rb').read()[44:]
even = b''.join(c0[i:i + 2] for i in range(0, 8000, 4))
check('first chunk keeps every other frame x2', out_pcm[:8000] == even + even)
check('second chunk kept in original order', out_pcm[8000:16000] == c1)
check('output differs from source', out_pcm != src_pcm)

os.write(master4, b'q')
time.sleep(1.0)
rc4 = p4.poll()
if rc4 is None:
    p4.kill(); p4.wait()
check('squish session quits', rc4 is not None)
os.close(master4)

# --- Fifth session: Stutter at 1/2 divides the chunk into 16ths, picks one
# --- at random, and repeats it for the whole chunk. ---
existing = len(slicer_files())
master5, slave5 = pty.openpty()
fcntl.ioctl(master5, termios.TIOCSWINSZ, struct.pack('HHHH', 40, 80, 0, 0))
p5 = subprocess.Popen(['./build/bin/App'], stdin=slave5, stdout=slave5,
                      stderr=slave5, close_fds=True)
os.close(slave5)
time.sleep(1.2)
state5 = {'buf': b'', 'last': b''}

def drain5(t=1.0, stop=None):
    end = time.time() + t
    out = b''
    while time.time() < end:
        r, _, _ = select.select([master5], [], [], 0.1)
        if r:
            try:
                c = os.read(master5, 65536)
            except OSError:
                break
            if not c:
                break
            state5['buf'] += c
            out += c
            if stop and stop.encode() in out:
                break
    state5['last'] = out
    return state5['buf']

drain5(0.8)

# Load ramp.wav (sliced/ still shifts the listing).
os.write(master5, b'\x0f'); drain5(1.0, 'Files:')
time.sleep(0.2); os.write(master5, b'\t'); time.sleep(0.3); drain5(0.5)
os.write(master5, b'\x1b[B'); time.sleep(0.3); drain5(0.5)
os.write(master5, b'\x1b[B'); time.sleep(0.3); drain5(0.5)
os.write(master5, b'\x1b[B'); time.sleep(0.3); drain5(0.5)
os.write(master5, b'\r'); time.sleep(0.8); drain5(1.2)

# 1 bar, 1/2 slices -> 2 slices (subdivision granularity = 1/16 of a chunk).
os.write(master5, b'\r'); time.sleep(0.4); drain5(0.8)
os.write(master5, b'\x1b[B'); time.sleep(0.3); drain5(0.5)
os.write(master5, b'\x1b[C'); time.sleep(0.3); drain5(0.5)
os.write(master5, b'\r'); time.sleep(0.5); drain5(1.0)
check('2 slices configured (stutter session)', b'Slices: 2' in state5['last'])

# Slice 1 -> Stutter, slice 2 -> None.
os.write(master5, b'\x1b[B'); time.sleep(0.3); drain5(0.6)
os.write(master5, b'\r'); time.sleep(0.4); drain5(1.0)     # open effect menu
os.write(master5, b'\x1b[B'); time.sleep(0.3); drain5(0.6)  # Shuffle -> Reverse
os.write(master5, b'\x1b[B'); time.sleep(0.3); drain5(0.6)  # -> Stretch
os.write(master5, b'\x1b[B'); time.sleep(0.3); drain5(0.6)  # -> Squish
os.write(master5, b'\x1b[B'); time.sleep(0.3); drain5(0.6)  # -> Stutter
os.write(master5, b'\r'); time.sleep(0.4); drain5(1.0)
check('slice 1 set to Stutter', b'     1  Stutter' in state5['last'])
os.write(master5, b'\x1b[B'); time.sleep(0.3); drain5(0.6)  # down -> slice 2
os.write(master5, b'\r'); time.sleep(0.4); drain5(1.0)     # open effect menu
for _ in range(5):
    os.write(master5, b'\x1b[B'); time.sleep(0.2); drain5(0.4)  # wrap to None
os.write(master5, b'\r'); time.sleep(0.4); drain5(1.0)
check('slice 2 set to None', b'     2  None' in state5['last'])

# Apply; chunk 1 must be one 500-byte sub-piece repeated 16 times.
os.write(master5, b'\x01'); time.sleep(1.5); drain5(1.5)
files = slicer_files()
check('stutter-session slice created', len(files) == existing + 1)
out_pcm = open(files[-1], 'rb').read()[44:]
st_blocks = [out_pcm[i:i + 500] for i in range(0, 8000, 500)]
check('stutter chunk repeats one sub-piece', len(set(st_blocks)) == 1)
cand = [c0[i:i + 500] for i in range(0, 8000, 500)]
check('stutter sub-piece comes from the chunk', st_blocks[0] in cand)
check('second chunk kept in original order', out_pcm[8000:16000] == c1)
check('output differs from source', out_pcm != src_pcm)

os.write(master5, b'q')
time.sleep(1.0)
rc5 = p5.poll()
if rc5 is None:
    p5.kill(); p5.wait()
check('stutter session quits', rc5 is not None)
os.close(master5)

if FAILURES:
    print(f"\nFAILED: {FAILURES}")
    sys.exit(1)
print("\nAll checks passed.")
sys.exit(0)