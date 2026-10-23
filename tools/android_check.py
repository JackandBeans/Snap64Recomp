#!/usr/bin/env python3
"""The Android app on an emulator or a device, driven through adb.

    python tools/android_check.py --rom <z64> --save <pokemonsnap.bin> --mod <snap64_zz_hooktest.nrm>
                                  --out <folder> [--apk <app-debug.apk>] [--avd <name> [--window]]
                                  [--uipad <uipad>] [--stages boot,mod,...] [--adb <adb>] [--serial <id>]
                                  [--cleanup]

On a phone (a test lab's, reached through adb) the device's model, Android
version, ABI, page size and chip are printed before anything is installed,
and a device whose ABI the app does not carry is refused. The stages run in
the order below, the quick proofs first, and each stage's files are pulled as
soon as it ends and every check is written to summary.txt as it lands, so a
reservation that ends early keeps what ran. --cleanup (or --stages cleanup
alone) uninstalls the app and removes every folder the run made on the
device, with the ROM, the save and the mod in them, and lists what is left.

The release suite (tools/release_check.py) and the Mac smoke
(tools/macos_smoke.py) run the executable with environment variables and read
what it prints. On Android the game is an app: adb installs it, the launching
intent's extras become the environment (Snap64Activity.java), and the game
folder named by SNAP_DATA_DIR is where it reads the ROM, the tape and the mods
and writes snap64.log, the frames and the photos. Each stage here gets a fresh
game folder on the device under /sdcard/Snap64Check holding only what it needs,
starts the app with the stage's extras, waits for the game's own marker in
snap64.log (a cap of seconds is only a distant safety), stops the app, and
pulls the log, the system log, the frames and the photos into one folder per
stage under --out. Each is judged by the rule the PC or Mac check uses for it.

The stages:

* boot: three presented frames (SNAP_PCAP_ATFRAME) with a picture on them,
  the version and "graphics API: Vulkan" lines, no crash. On Android the
  start goes fullscreen (src/main.cpp), so the screen is photographed and the
  side bars beside the 4:3 picture must be dark: the system's status icons are
  hidden. The single-instance lock must have been taken ("cannot lock" absent).
  The Vulkan device and driver lines are printed.
  The start's Vulkan check (src/gpu_check.cpp) must name the device and not
  refuse it.
* vkcheck: SNAP_VK_TEST=1.1 has the device answer Vulkan 1.1: the port must
  refuse it in the log, show its message in Android's own dialog (read off
  the screen with uiautomator), wait on the dialog, end by itself once its
  OK is pressed, and start no renderer.
* mod: Snap64RecompMods' zz_hooktest in mods/: its dmaCopy hook runs, and the
  system log has no SELinux denial for the app.
* lifecycle: Home and back, the recent apps, the screen off and on, the
  notification shade and the other landscape: after each the game still
  steps (new [SNAP-TICK] lines) and the screen shows a picture again.
* touch: the touch controls by touch alone, from the intro to a course
  (src/touch_controls.cpp): the overlay drawn where its layout says, Start
  through the intro, a tap on the title's Continue, taps on Go to Course and
  the Beach, a drag that aims, quick taps, a key that hides the controls.
  Z, in the game's Hold mode, is held by a tap and let go by the next (the
  game zooms in and out, Z drawn lit between), and a key that hides the
  controls lets it go without showing them again. On an emulator (adb as
  root) Z is tapped while a second finger presses A, and a finger resting
  on the picture neither keeps the aim from one that moves nor aims while
  it does. The first course's hint shows over the picture and goes, and A
  pressed zoomed in asks for the firmer feedback.
* touchswitch: the same way to a course on a copy of the save whose Z
  Button is Switch, with the buttons at touch_size 50: Start is pressed
  outside its drawn circle but inside its 48 dp square, a tap on Z is a
  plain press, the game zooms in at one and out at the next, and a tap at
  C-Up's center, where a neighbor's square overlaps, is C-Up's.
* touchwide: the same way to a course with Widescreen and Wide HUD on: while
  the touch controls show, the film counter keeps its 4:3 place, clear of R
  in the wide picture's top corner; while they hide (a key), Wide HUD puts
  it in that corner; shown again, it is back (src/rect_tags.cpp).
* gyro: the same way to a course with Gyro Aim on and SNAP_GYRO_DEBUG: the
  phone's own gyroscope opens; zoomed in, the emulator's gyroscope turns
  the phone left and then raises it for a second each (its accelerometer
  set to say the screen's up is up), and the view turns left and rises by
  the angle the readings made, within a tenth; a still phone for three
  seconds turns nothing. Needs an emulator (adb emu sensor).
* pad: on an emulator (adb as root) with --uipad, a game pad made through
  Linux's uinput (tools/android/uipad.c), which Android and SDL take for a
  real one: it is opened in place of the emulator's own keyboard (which SDL
  maps as a pad with no stick); with SNAP_RECORD on, Start goes through the
  intro and every button, the D-pad and both sticks are read as the N64
  input the default bindings give them (the recorded tape, press by press);
  the touch controls hide at the pad's first press and come back at a
  touch; the right shoulder fast-forwards, the left stick pressed in slows
  the game, Back opens the pages. On a phone it is not run.
* score: the scoring tape (eval.inputs, on the save it was recorded with).
  Every [SNAP-SCORE] line inside the healthy signature, at least 20 of them;
  in step with the Windows reference (tools/replays/eval.scores) photo by
  photo and how many of its 45 lines are identical, reported; at least 10
  photos exported. The screen is photographed during the ride.
* stats: the Beach tape with SNAP_STATS, to the 30th [SNAP-TICK] line (the
  game time the PC's 130 seconds cover): the pacing, coherence and tick lines
  in the PC check's numbers, no hold failures, the [SNAP-VSYNC] line. On an
  emulator the coherence lines need only be there (50 or more): their count
  follows its speed, which swings from run to run.
* attract: the Beach tape with no diagnostics, captures at readings 900 and
  1800: every captured frame has a picture on it.
* menu: the eval tape to the Options page's staged interface strings, none
  withheld.
* pages: the title tape with the menu key at readings 300 and 700
  (SNAP_MENU_AT): the pages open, close, and the music is held with them.
* settings: snapsettings.json written by a first run parses and has the fields
  the PC check names; with widescreen and a volume of 37 written into it, a
  second run applies widescreen and keeps the volume.
* rompick: a folder with no ROM, answered by SNAP_ROM_PICK: a z64 dump copied
  in byte-identical, a byte-swapped dump copied in big-endian order, and a
  Cancel that copies nothing and starts no renderer.

The suite's speed and slow checks hold the key for a whole run and time it
against the wall clock, which an emulator's own pace makes meaningless; the
station check needs the port-4 printer path and a relaunch, which Android does
not have; vram, gpufail and nsopad test Windows and PC pad paths; subsystem,
stdio and package are about the PC executable and its archive. None of them
is run here.

The exit code is 1 when a check failed.
"""
import argparse
import hashlib
import json
import math
import os
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import time

TOOLS = pathlib.Path(__file__).resolve().parent
REPLAYS = TOOLS / 'replays'
PKG = 'io.github.jackandbeans.snap64recomp'
LAUNCHER = PKG + '/.Snap64Launcher'
ROOT = '/sdcard/Snap64Check'
# The order a reservation that can end at any minute wants: the quick proofs
# first (boot, the mod's hooks), then the tapes.
ALL_STAGES = 'boot,vkcheck,mod,lifecycle,touch,touchswitch,touchwide,gyro,pad,score,stats,attract,menu,pages,settings,rompick'


class Checks:
    def __init__(self, log=None):
        self.rows = []
        self.log = log

    def add(self, stage, ok, text):
        self.rows.append((stage, bool(ok), text))
        line = '%s  %-10s %s' % ('PASS' if ok else 'FAIL', stage, text)
        print(line, flush=True)
        if self.log is not None:
            # Written as each check lands, so a run cut off keeps what ran.
            with open(self.log, 'a', encoding='utf-8') as f:
                f.write(line + '\n')

    def failed(self):
        return [r for r in self.rows if not r[1]]


class Device:
    """adb, for one device."""

    def __init__(self, adb, serial):
        self.adb = adb
        self.serial = serial

    def run(self, args, timeout=300, binary=False):
        cmd = [self.adb] + (['-s', self.serial] if self.serial else []) + list(args)
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
        return p.stdout if binary else p.stdout.decode('utf-8', errors='replace')

    def sh(self, command, timeout=300):
        return self.run(['shell', command], timeout=timeout)

    def push(self, local, remote):
        self.sh('mkdir -p %s' % posix_dir(remote))
        out = self.run(['push', str(local), remote], timeout=600)
        if 'pushed' not in out:
            raise RuntimeError('adb push %s %s: %s' % (local, remote, out.strip()))

    def pull(self, remote, local):
        out = self.run(['pull', remote, str(local)], timeout=600)
        return 'pulled' in out

    def exists(self, remote):
        return self.sh('test -e %s && echo yes || echo no' % remote).strip().endswith('yes')

    def cat(self, remote):
        if not self.exists(remote):
            return None
        return self.run(['exec-out', 'cat', remote], binary=True)

    def alive(self):
        return self.sh('pidof %s' % PKG).strip() != ''

    def online(self):
        return self.run(['get-state'], timeout=30).strip() == 'device'

    def start(self, extras):
        # The first course's hint (Snap64TouchOverlay) only where a stage
        # asks for it: no other stage's screen has it.
        extras = dict({'SNAP_TOUCH_HINT': 'off'}, **extras)
        args = ['shell', 'am', 'start', '-W', '-n', LAUNCHER]
        for k, v in extras.items():
            args += ['-e', k, str(v)]
        return self.run(args)

    def stop(self):
        self.sh('am force-stop %s' % PKG)

    def screencap(self, local):
        self.sh('screencap -p /sdcard/snap64check_shot.png')
        ok = self.pull('/sdcard/snap64check_shot.png', local)
        self.sh('rm -f /sdcard/snap64check_shot.png')
        return ok

    def tail_from(self, remote, offset):
        """The file's bytes past offset (nothing when it is not there yet):
        a growing log read in pieces, which a slow link needs."""
        return self.run(['exec-out', 'tail -c +%d %s 2>/dev/null' % (offset + 1, remote)], binary=True)

    def logcat_to(self, local, buffers):
        # The game's own lines (tag Snap64) are snap64.log's again; on a slow
        # link they are most of the system log's size, so they are left out.
        data = self.run(['exec-out', "logcat -d -b %s | grep -v -F ' Snap64  : '" % buffers], binary=True, timeout=900)
        local.write_bytes(data)


def posix_dir(path):
    return path.rsplit('/', 1)[0] or '/'


def text_lines(data):
    return data.decode('utf-8', errors='replace').splitlines() if data else []


def bmp_mean(path):
    """The mean luminance of a BMP the frame capture wrote (24 or 32 bits,
    uncompressed), from a sparse grid of its pixels."""
    data = path.read_bytes()
    if data[:2] != b'BM':
        return None
    offset = struct.unpack_from('<I', data, 10)[0]
    width, height = struct.unpack_from('<ii', data, 18)
    bpp = struct.unpack_from('<H', data, 28)[0]
    if bpp not in (24, 32):
        return None
    step = bpp // 8
    row = ((width * step + 3) // 4) * 4
    total = 0
    count = 0
    for y in range(0, abs(height), max(1, abs(height) // 60)):
        for x in range(0, width, max(1, width // 80)):
            at = offset + y * row + x * step
            b, g, r = data[at], data[at + 1], data[at + 2]
            total += 0.299 * r + 0.587 * g + 0.114 * b
            count += 1
    return total / count if count else None


def png_gray(path):
    """(width, height, rows of luminance) of an 8-bit RGB or RGBA PNG, as
    screencap writes them: through Pillow when it is there, else decoded here
    (slowly, with the standard library alone)."""
    try:
        from PIL import Image
        im = Image.open(path).convert('L')
        w, h = im.size
        px = list(im.getdata())
        return w, h, [px[y * w:(y + 1) * w] for y in range(h)]
    except ImportError:
        pass
    import zlib
    data = path.read_bytes()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        return None
    pos = 8
    width = height = None
    channels = 0
    idat = b''
    while pos < len(data):
        length = struct.unpack_from('>I', data, pos)[0]
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if kind == b'IHDR':
            width, height, depth, color = struct.unpack_from('>IIBB', body, 0)
            if depth != 8 or color not in (2, 6):
                return None
            channels = 3 if color == 2 else 4
        elif kind == b'IDAT':
            idat += body
        elif kind == b'IEND':
            break
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * channels
    rows = []
    prev = bytearray(stride)
    at = 0
    for _ in range(height):
        f = raw[at]
        line = bytearray(raw[at + 1:at + 1 + stride])
        at += 1 + stride
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = prev[i]
            c = prev[i - channels] if i >= channels else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if (pa <= pb and pa <= pc) else (b if pb <= pc else c))) & 255
        rows.append([(299 * line[x] + 587 * line[x + 1] + 114 * line[x + 2]) // 1000 for x in range(0, stride, channels)])
        prev = line
    return width, height, rows


class Stage:
    """One run of the app in a fresh game folder."""

    def __init__(self, dev, out, name, folder=None):
        self.dev = dev
        self.name = name
        self.folder = folder or (ROOT + '/' + name)
        self.local = out / name
        self.lines = []
        self.ended = False       # the app was gone before it was stopped
        self.met = False
        self.took = 0.0
        self.tag = ''

    def fresh(self, files):
        """files: (local path, path inside the game folder)."""
        if self.local.exists():
            shutil.rmtree(self.local)
        self.local.mkdir(parents=True)
        self.dev.sh('rm -rf %s; mkdir -p %s' % (self.folder, self.folder))
        for local, inside in files:
            self.dev.push(local, self.folder + '/' + inside)

    def run(self, extras, done, after, cap, keep=True, tag=''):
        """Starts the app with the extras, waits for done(lines) and `after`
        seconds more, or `cap` seconds, or the app's end; stops it and pulls
        what it wrote. tag names a second run's files."""
        env = {'SNAP_DATA_DIR': self.folder, 'SNAP_MUTE': '1', 'SNAP_ROM_PICK': 'cancel'}
        env.update(extras)
        self.dev.stop()
        self.dev.sh('rm -f %s/snap64.log %s/snap64.prev.log' % (self.folder, self.folder))
        self.dev.run(['logcat', '-b', 'all', '-c'])
        self.dev.start(env)
        t0 = time.time()
        met_at = None
        self.ended = False
        log = self.folder + '/snap64.log'
        data = b''
        while True:
            time.sleep(3.0)
            if not self.dev.online():
                raise RuntimeError('the device went away %.0f s into the stage' % (time.time() - t0))
            data += self.dev.tail_from(log, len(data))
            self.lines = text_lines(data)
            now = time.time()
            if met_at is None and done(self.lines):
                met_at = now
            if met_at is not None and now - met_at >= after:
                break
            if not self.dev.alive():
                self.ended = True
                data += self.dev.tail_from(log, len(data))
                self.lines = text_lines(data)
                break
            if now - t0 >= cap:
                break
        self.took = time.time() - t0
        self.met = met_at is not None
        self.tag = tag
        self.dev.stop()
        time.sleep(1.0)
        data += self.dev.tail_from(log, len(data))
        self.lines = text_lines(data)
        (self.local / ('snap64%s.log' % tag)).write_text('\n'.join(self.lines) + '\n', encoding='utf-8')
        self.dev.logcat_to(self.local / ('logcat%s.txt' % tag), 'all')
        self.dev.logcat_to(self.local / ('crash%s.txt' % tag), 'crash')
        if keep:
            for sub in ('snap_frame_dumps', 'photos'):
                if self.dev.exists(self.folder + '/' + sub):
                    self.dev.pull(self.folder + '/' + sub, self.local)
        return self.lines

    def crashed(self):
        """A crash report from the port ([SNAP-AV]) or the system's crash
        buffer naming the app, in the last run."""
        crash = (self.local / ('crash%s.txt' % self.tag)).read_bytes().decode('utf-8', errors='replace')
        mine = [l for l in crash.splitlines() if PKG in l or 'snap64' in l.lower()]
        av = [l for l in self.lines if l.startswith('[SNAP-AV]')]
        return (av + mine)[:2]

    def judge_end(self, checks, expect_end=False):
        why = self.crashed()
        if why:
            checks.add(self.name, False, 'crash: %s' % ' | '.join(why))
        elif self.ended and not expect_end:
            checks.add(self.name, False, 'the app ended by itself after %.0f s; the log ends: %s'
                       % (self.took, ' | '.join(l for l in self.lines[-3:] if l.strip())))
        elif not self.met:
            checks.add(self.name, False, 'the marker never came in %.0f s (cap)' % self.took)
        else:
            checks.add(self.name, True, 'marker reached in %.0f s, no crash' % self.took)


def frames_of(stage):
    d = stage.local / 'snap_frame_dumps'
    return sorted(d.glob('*.bmp')) if d.exists() else []


def count(lines, prefix):
    return sum(1 for l in lines if l.startswith(prefix))


def stage_boot(checks, dev, out, a):
    s = Stage(dev, out, 'boot')
    s.fresh([(a.rom, 'pokemonsnap.z64')])
    shot = {}

    def three(lines):
        n = count(lines, '[SNAP-PCAP] wrote')
        if n >= 3 and 'shot' not in shot:
            shot['shot'] = dev.screencap(s.local / 'screen.png')
        return n >= 3

    s.run({'SNAP_PCAP_ATFRAME': '300,900,1500', 'SNAP_PCAP_BURST': '1', 'SNAP_STATS': '1'}, three, 3.0, 600)
    lines = s.lines
    version = next((l for l in lines if l.startswith('[SNAP] Snap64 Recomp ')), None)
    checks.add('boot', version is not None, version or 'no version line')
    api = next((l for l in lines if 'graphics API' in l), None)
    checks.add('boot', api is not None and 'Vulkan' in api, api or 'no graphics API line')
    # The start's Vulkan check (src/gpu_check.cpp) names the devices and
    # lets this one through.
    vk = [l for l in lines if l.startswith('[SNAP-VK] device:')]
    refused = next((l for l in lines if l.startswith('[SNAP-VK] refused')), None)
    checks.add('boot', bool(vk) and refused is None,
               '%s; not refused' % vk[0] if vk and refused is None else (refused or 'no "[SNAP-VK] device:" line'))
    frames = frames_of(s)
    means = [bmp_mean(f) for f in frames]
    lit = [m for m in means if m is not None and m > 12]
    checks.add('boot', len(lit) >= 1, '%d frames captured, %d with a picture (mean luminance %s) in %.0f s'
               % (len(frames), len(lit), ', '.join('%.0f' % m for m in means if m is not None) or '-', s.took))
    rates = [float(m.group(1)) for m in (re.search(r'average [\d.]+ ms \(([\d.]+) fps\)', l) for l in lines) if m]
    if rates:
        print('           game frame rate over %d reports: mean %.1f fps, lowest %.1f, highest %.1f'
              % (len(rates), sum(rates) / len(rates), min(rates), max(rates)), flush=True)
    for l in lines:
        if l.startswith(('Device Name:', 'Device Vendor:', 'Driver Version:')) or 'driver' in l.lower() and 'vulkan' in l.lower():
            print('           %s' % l, flush=True)
    # A phone's Vulkan driver says its own build in the system log when it
    # loads (Qualcomm's "QUALCOMM build", Arm's "Mali" lines).
    logcat = (s.local / 'logcat.txt').read_bytes().decode('utf-8', errors='replace').splitlines()
    driver = [l.split(': ', 1)[-1].strip() for l in logcat
              if re.search(r'QUALCOMM build|Build Date|Build Config|Driver Path|mali.*(version|r\d+p\d+)', l, re.I)]
    for l in driver[:5]:
        print('           driver (system log): %s' % l, flush=True)
    full = next((l for l in lines if l.startswith('[SNAP] fullscreen:')), None)
    checks.add('boot', full is not None, full or 'no "[SNAP] fullscreen:" line')
    lock = next((l for l in lines if 'cannot lock' in l), None)
    checks.add('boot', lock is None, lock or 'the single-instance lock was taken (no "cannot lock" line)')
    # The screen: a 4:3 picture in a wider screen leaves dark bars at the
    # sides, where the status bar's clock and icons would be if it showed.
    g = png_gray(s.local / 'screen.png') if (s.local / 'screen.png').exists() else None
    if g is None:
        checks.add('boot', False, 'no screenshot')
    else:
        w, h, rows = g
        # Where a 4:3 picture the screen's height tall stands, centered; the
        # bars beside it are the screen's own black.
        left = (w - (h * 4) // 3) // 2
        right = w - 1 - left
        mid = [rows[y][w // 2] for y in range(h)]
        lit_rows = [y for y, v in enumerate(mid) if v > 16]
        cols = [x for x in range(w) if rows[h // 2][x] > 16]
        # Only the status bar's own rows (about 24 dp, the top 5.5%): lower
        # down, the touch controls stand in the bars.
        side = 0
        bar_rows = max(1, int(h * 0.055))
        for y in range(bar_rows):
            r = rows[y]
            side = max(side, max(r[:max(0, left - 4)] or [0]), max(r[right + 5:] or [0]))
        checks.add('boot', w > h and side <= 16,
                   'screen %dx%d; the middle row lit from x %s to %s, the middle column from y %s to %s; brightest '
                   'pixel beside x %d to %d in the top %d rows: %d (the status bar\'s clock and icons would be bright)'
                   % (w, h, cols[0] if cols else '-', cols[-1] if cols else '-', lit_rows[0] if lit_rows else '-',
                      lit_rows[-1] if lit_rows else '-', left, right, bar_rows, side))
    s.judge_end(checks)


def score_lines(lines):
    return [l for l in lines if l.startswith('[SNAP-SCORE] pokemon')]


def score_groups(lines):
    """How many subjects each scored photo had: a line's pokemon 0 starts a photo."""
    groups = []
    for l in lines:
        n = int(l.split('pokemon ')[1].split(':')[0])
        if n == 0:
            groups.append(0)
        if groups:
            groups[-1] += 1
    return groups


def stage_score(checks, dev, out, a):
    s = Stage(dev, out, 'score')
    s.fresh([(a.rom, 'pokemonsnap.z64'), (a.save, 'saves/pokemonsnap.bin'), (REPLAYS / 'eval.inputs', 'eval.inputs')])

    shot = {}

    def done(ls):
        # The ride on the screen: 15 seconds after its first Pokemon spawned
        # (the photos are scored in one go once the ride is over).
        if 'spawn' not in shot and any(l.startswith('[SNAP-SPAWN]') for l in ls):
            shot['spawn'] = time.time()
        if 'spawn' in shot and 'ride' not in shot and time.time() - shot['spawn'] >= 15:
            shot['ride'] = dev.screencap(s.local / 'screen-ride.png')
        return len(score_lines(ls)) >= 45 or any(l.startswith('[SNAP-PHOTO] render #') for l in ls)

    s.run({'SNAP_REPLAY': 'eval.inputs', 'SNAP_STATS': '1', 'SNAP_PHOTO_AUTOEXPORT': '1'}, done, 90.0, 2400)
    scores = score_lines(s.lines)
    bad = []
    for l in scores:
        try:
            f = l.split()
            odd = int(f[f.index('odd') + 1]); u = int(f[f.index('U') + 1]); eq = int(f[f.index('eq') + 1])
            or1 = int(f[f.index('or1') + 1]); rec = int(f[f.index('recomputed') + 1])
            game = int(f[f.index('game') + 1].rstrip('*'))
        except (ValueError, IndexError):
            bad.append(l)
            continue
        if odd != 0 or eq != u or or1 != 0 or rec != game:
            bad.append(l)
    ref = [l for l in (REPLAYS / 'eval.scores').read_text(encoding='utf-8').splitlines() if l.strip()]
    ref_groups = score_groups(ref)
    got_groups = score_groups(scores)
    in_step = 0
    for x, y in zip(got_groups, ref_groups):
        if x != y:
            break
        in_step += 1
    checks.add('score', len(scores) >= 20 and not bad,
               '%d scored lines in %.0f s (the reference has 45), %d outside the healthy signature%s; '
               'in step with Windows for %d of its %d photos' % (len(scores), s.took, len(bad),
                                                                 (': ' + bad[0]) if bad else '', in_step, len(ref_groups)))
    same = sum(1 for x, y in zip(scores, ref) if x == y)
    print('           against the Windows build: %d of %d score lines identical' % (same, len(ref)), flush=True)
    for x, y in zip(scores, ref):
        if x != y:
            print('             android: %s\n             windows: %s' % (x, y), flush=True)
    photos = list((s.local / 'photos').glob('*.png')) if (s.local / 'photos').exists() else []
    checks.add('score', len(photos) >= 10, '%d photos exported' % len(photos))
    s.judge_end(checks)


def stage_attract(checks, dev, out, a):
    s = Stage(dev, out, 'attract')
    s.fresh([(a.rom, 'pokemonsnap.z64'), (a.save, 'saves/pokemonsnap.bin'), (REPLAYS / 'beach.inputs', 'beach.inputs')])
    s.run({'SNAP_REPLAY': 'beach.inputs', 'SNAP_PCAP_AT': '900,1800', 'SNAP_PCAP_BURST': '2'},
          lambda ls: count(ls, '[SNAP-PCAP] wrote') >= 4, 3.0, 900)
    means = [m for m in (bmp_mean(f) for f in frames_of(s)) if m is not None]
    bright = [m for m in means if m > 12.0]
    checks.add('attract', len(means) >= 3 and len(bright) == len(means),
               '%d frames captured, %d with a picture on them (mean luminance > 12): %s'
               % (len(means), len(bright), ', '.join('%.0f' % m for m in means)))
    s.judge_end(checks)


def stage_stats(checks, dev, out, a):
    s = Stage(dev, out, 'stats')
    # The PC check's numbers come from the suite's own settings file, whose
    # frame rate is the display's (fps_mode 1): a present between two game
    # frames is what prints a coherence line, and a pacing report comes every
    # 600 presents. A fresh folder's default is the game's own 30 (fps_mode
    # 0), with neither, so the stage's folder gets the suite's choice.
    settings = s.local.parent / 'stats-settings.json'
    settings.write_text('{"fps_mode": 1}\n', encoding='utf-8')
    s.fresh([(a.rom, 'pokemonsnap.z64'), (a.save, 'saves/pokemonsnap.bin'), (REPLAYS / 'beach.inputs', 'beach.inputs'),
             (settings, 'snapsettings.json')])
    s.run({'SNAP_REPLAY': 'beach.inputs', 'SNAP_STATS': '1'}, lambda ls: count(ls, '[SNAP-TICK]') >= 30, 3.0, 1800)
    ls = s.lines
    pace = sum(1 for l in ls if l.startswith('[SNAP-PACE]') and ' presents, average ' in l)
    coh = count(ls, '[SNAP-COH]')
    tick = count(ls, '[SNAP-TICK]')
    holdfail = count(ls, '[SNAP-HOLDFAIL]')
    checks.add('stats', pace >= 20, '%d pacing reports to the 30th tick line, in %.0f s' % (pace, s.took))
    # A coherence line is printed only when an interpolated present snaps or
    # frees an object, so the count follows the device's own timing. A phone
    # is held to the PC check's 500 (the S25 gave 1083); Google's emulator,
    # whose speed swings from run to run, gave 99, 244, 302 and 572 for the
    # same tape on Oct 21 and 22 2026, last night's APK among them, so there
    # the lines need only be there.
    emulator = dev.sh('getprop ro.kernel.qemu').strip() == '1' or dev.sh('getprop ro.hardware').strip() == 'ranchu'
    floor = 50 if emulator else 500
    checks.add('stats', coh >= floor and tick >= 30, '%d coherence lines (%s %d), %d tick lines'
               % (coh, 'the emulator\'s floor' if emulator else 'at least', floor, tick))
    checks.add('stats', holdfail == 0, '%d hold failures' % holdfail)
    vsync = [l for l in ls if l.startswith('[SNAP-VSYNC]')]
    checks.add('stats', bool(vsync), vsync[0] if vsync else 'no [SNAP-VSYNC] line')
    rates = [float(m.group(1)) for m in (re.search(r'average [\d.]+ ms \(([\d.]+) fps\)', l) for l in ls
                                         if l.startswith('[SNAP-TICK]')) if m]
    if rates:
        print('           game frame rate over %d tick reports: mean %.1f fps, lowest %.1f, highest %.1f'
              % (len(rates), sum(rates) / len(rates), min(rates), max(rates)), flush=True)
    # A pacing report is printed every 600 presents and a coherence line only
    # for a present between two game frames: both counts follow the present
    # rate, which the PC check's numbers take to be the display's.
    shown = [float(m.group(1)) for m in (re.search(r'presents, average [\d.]+ ms \(([\d.]+) fps\)', l) for l in ls) if m]
    if shown:
        print('           presents: mean %.1f a second over %d pacing reports (the PC check\'s counts assume the '
              'display\'s 60 or more)' % (sum(shown) / len(shown), len(shown)), flush=True)
    s.judge_end(checks)


def stage_menu(checks, dev, out, a):
    s = Stage(dev, out, 'menu')
    s.fresh([(a.rom, 'pokemonsnap.z64'), (a.save, 'saves/pokemonsnap.bin'), (REPLAYS / 'eval.inputs', 'eval.inputs')])
    s.run({'SNAP_REPLAY': 'eval.inputs'}, lambda ls: count(ls, '[SNAP-MENU] staged ') >= 1, 3.0, 600, keep=False)
    staged = [l for l in s.lines if l.startswith('[SNAP-MENU] staged ')]
    withheld = [l for l in s.lines if 'the staged strings are withheld' in l]
    missing = [l for l in s.lines if l.startswith('[SNAP-MENU] no glyph for ')]
    detail = ('staged (' + staged[0].split('staged ', 1)[1] + ')') if staged else 'NOT staged'
    if withheld:
        detail += '; withheld: ' + '; '.join(m.split('] ', 1)[1] for m in missing)
    checks.add('menu', bool(staged) and not withheld, 'interface strings ' + detail)
    s.judge_end(checks)


def stage_pages(checks, dev, out, a):
    s = Stage(dev, out, 'pages')
    s.fresh([(a.rom, 'pokemonsnap.z64'), (a.save, 'saves/pokemonsnap.bin'),
             (REPLAYS / 'menu_title.inputs', 'menu_title.inputs')])
    closed = re.compile(r'\[SNAP-DL\] with the pages up the main buffer reached (\d+) of (\d+) bytes')
    s.run({'SNAP_REPLAY': 'menu_title.inputs', 'SNAP_MENU_AT': '300,700'},
          lambda ls: any(closed.match(l) for l in ls), 5.0, 900, keep=False)
    lines = s.lines
    opened = [l for l in lines if l.startswith('[SNAP-MENU] the pages from here: opened on scene ')]
    hung = [l for l in lines if l.startswith('[SNAP-HANG] no game logic step')]
    overrun = [l for l in lines if '*** OVERFLOW ***' in l]
    checks.add('pages', bool(opened) and not hung and not overrun,
               (opened[0].split('] ', 1)[1] if opened else 'the runner never said the pages opened')
               + ('; HUNG' if hung else '') + ('; display list OVERRUN' if overrun else ''))
    fit = None
    for l in lines:
        m = closed.match(l)
        if m:
            fit = (int(m.group(1)), int(m.group(2)))
    checks.add('pages', fit is not None and fit[0] < fit[1],
               ('closed by the key; the display list reached %d of %d bytes with them up' % fit) if fit
               else 'the pages never closed (no display-list line)')
    held = [l for l in lines if l.startswith('[SNAP-BGM] ')]
    checks.add('pages', bool(held) and all(' stood ' in l for l in held),
               (held[0].split('] ', 1)[1] if held else 'the music was never held with the screen')
               + ('' if all(' stood ' in l for l in held) else '; a player MOVED while held'))
    s.judge_end(checks)


def stage_settings(checks, dev, out, a):
    s = Stage(dev, out, 'settings')
    s.fresh([(a.rom, 'pokemonsnap.z64')])
    title = lambda ls: any('menu font harvested' in l for l in ls)
    s.run({}, title, 3.0, 400, keep=False, tag='-1')
    path = s.folder + '/snapsettings.json'
    first = dev.cat(path)
    (s.local / 'snapsettings-1.json').write_bytes(first or b'')
    try:
        j = json.loads((first or b'').decode('utf-8'))
    except ValueError as e:
        checks.add('settings', False, 'snapsettings.json after the first run: %s' % (e if first else 'not written'))
        return
    need = ['fps_mode', 'resolution_scale', 'crop_enabled', 'intro_fix', 'photo_detail', 'jynx_vc', 'master_volume']
    missing = [k for k in need if k not in j]
    checks.add('settings', not missing, 'written by the first run: valid JSON with %d fields%s'
               % (len(j), (', missing ' + ', '.join(missing)) if missing else ''))
    j['widescreen'] = True
    j['master_volume'] = 37
    edited = s.local / 'snapsettings-edited.json'
    edited.write_text(json.dumps(j, indent=2), encoding='utf-8')
    dev.push(edited, path)
    s.run({}, title, 3.0, 400, keep=False, tag='-2')
    applied = [l for l in s.lines if l.startswith('[SNAP-CFG] applied:')]
    second = dev.cat(path)
    (s.local / 'snapsettings-2.json').write_bytes(second or b'')
    try:
        j2 = json.loads((second or b'').decode('utf-8'))
    except ValueError:
        j2 = {}
    wide = any(', widescreen,' in l for l in applied)
    kept = j2.get('master_volume') == 37 and j2.get('widescreen') is True
    checks.add('settings', wide and kept, 'the second run read it back: %s; the file then holds widescreen %s, volume %s'
               % (applied[0] if applied else 'no applied line', j2.get('widescreen'), j2.get('master_volume')))
    s.judge_end(checks)


def stage_rompick(checks, dev, out, a):
    z64 = pathlib.Path(a.rom).read_bytes()
    want = hashlib.sha256(z64).hexdigest()
    local = out / 'rompick'
    if local.exists():
        shutil.rmtree(local)
    local.mkdir(parents=True)
    v64 = bytearray(z64)
    for i in range(0, len(v64) - 1, 2):
        v64[i], v64[i + 1] = v64[i + 1], v64[i]
    (local / 'dump.v64').write_bytes(bytes(v64))
    dev.sh('rm -rf %s/rompick; mkdir -p %s/rompick' % (ROOT, ROOT))
    dev.push(a.rom, ROOT + '/rompick/dump.z64')
    dev.push(local / 'dump.v64', ROOT + '/rompick/dump.v64')
    (local / 'dump.v64').unlink()
    settings = local / 'windowed.json'
    settings.write_text('{"fullscreen": false}\n', encoding='utf-8')
    cases = [('z64', ROOT + '/rompick/dump.z64', 300), ('v64', ROOT + '/rompick/dump.v64', 300), ('cancel', 'cancel', 120)]
    for name, pick, cap in cases:
        s = Stage(dev, out, 'rompick/' + name, folder=ROOT + '/rompick/rig_' + name)
        s.fresh([(settings, 'snapsettings.json')])
        if name == 'cancel':
            done = lambda ls: any('the chooser was canceled' in l for l in ls)
        else:
            done = lambda ls: any(l.startswith('[SNAP] surface') for l in ls)
        s.run({'SNAP_ROM_PICK': pick}, done, 3.0, cap, keep=False)
        out_text = '\n'.join(s.lines)
        asked = '[SNAP] ROM: none at' in out_text
        dest = s.folder + '/pokemonsnap.z64'
        if name == 'cancel':
            has = dev.exists(dest)
            ok = asked and ('the chooser was canceled' in out_text) and not has and ('[SNAP] surface' not in out_text)
            # The game's activity ends when the native main returns (SDL's
            # own log line); Android may keep the process for a while after.
            logcat = (s.local / 'logcat.txt').read_bytes().decode('utf-8', errors='replace')
            finished = 'Finished main function' in logcat
            checks.add('rompick', ok, 'cancel: asked %s, no file %s, no renderer %s; the game %s'
                       % (asked, not has, '[SNAP] surface' not in out_text,
                          'ended by itself (SDL: "Finished main function")' if finished else 'did NOT end by itself'))
        else:
            got = dev.cat(dest)
            copied = got is not None and hashlib.sha256(got).hexdigest() == want
            booted = '[SNAP] surface' in out_text
            order = 'byte-swapped' if name == 'v64' else 'big-endian'
            ok = asked and copied and booted and (('(%s' % order) in out_text)
            checks.add('rompick', ok, '%s: asked %s, copied byte-identical %s (%s named), booted %s'
                       % (name, asked, copied, order, booted))
    dev.sh('rm -rf %s/rompick' % ROOT)


def stage_mod(checks, dev, out, a):
    s = Stage(dev, out, 'mod')
    s.fresh([(a.rom, 'pokemonsnap.z64'), (a.mod, 'mods/' + pathlib.Path(a.mod).name)])
    s.run({}, lambda ls: any('[hooktest] dmaCopy hook fired' in l for l in ls), 10.0, 400, keep=False)
    fired = [l for l in s.lines if l.startswith('[hooktest]')]
    kinds = sorted(set(re.sub(r' hook fired.*', '', l) for l in fired))
    checks.add('mod', any('dmaCopy' in k for k in kinds),
               'the mod\'s hooks ran: %s (%d lines in %.0f s)' % (', '.join(kinds) or 'none', len(fired), s.took))
    logcat = (s.local / 'logcat.txt').read_bytes().decode('utf-8', errors='replace').splitlines()
    denied = [l for l in logcat if 'avc:' in l and 'denied' in l and 'untrusted_app' in l]
    checks.add('mod', not denied, 'SELinux denials for the app in the system log: %s'
               % (('%d, the first: %s' % (len(denied), denied[0].strip())) if denied else 'none'))
    s.judge_end(checks)


# What a phone does to an app that a PC never does: each event is done, held
# for a few seconds, and undone with the adb commands beside it.
LIFECYCLE = [
    ('home', 'Home, then back to the game',
     ['input keyevent KEYCODE_HOME'], ['@start']),
    ('recents', 'the recent-apps screen, then Back',
     ['input keyevent KEYCODE_APP_SWITCH'], ['input keyevent KEYCODE_BACK']),
    ('screen', 'the screen off and on again',
     ['input keyevent KEYCODE_SLEEP'], ['input keyevent KEYCODE_WAKEUP', 'wm dismiss-keyguard']),
    ('shade', 'the notification shade pulled down and closed',
     ['cmd statusbar expand-notifications'], ['cmd statusbar collapse']),
    ('turn', 'the phone turned upside down (the other landscape)',
     ['cmd window user-rotation lock 3'], ['cmd window user-rotation free']),
]


def picture_lit(path):
    """How many rows down the middle of a screenshot are lit, of how many."""
    g = png_gray(path) if path.exists() else None
    if g is None:
        return 0, 0
    w, h, rows = g
    return sum(1 for y in range(h) if rows[y][w // 2] > 16), h


def stage_lifecycle(checks, dev, out, a):
    """The game to the background and back, the screen off and on, the shade,
    the recent apps and the other landscape: after each, the game must still
    be running, its logic must go on (new [SNAP-TICK] lines) and the screen
    must show a picture again; nothing may crash."""
    s = Stage(dev, out, 'lifecycle')
    s.fresh([(a.rom, 'pokemonsnap.z64')])
    extras = {'SNAP_DATA_DIR': s.folder, 'SNAP_MUTE': '1', 'SNAP_ROM_PICK': 'cancel', 'SNAP_STATS': '1'}
    log = s.folder + '/snap64.log'
    data = [b'']

    def lines():
        data[0] += dev.tail_from(log, len(data[0]))
        return text_lines(data[0])

    dev.stop()
    dev.run(['logcat', '-b', 'all', '-c'])
    dev.start(extras)
    t0 = time.time()
    while time.time() - t0 < 300 and not any('menu font harvested' in l for l in lines()):
        time.sleep(3)
    time.sleep(8)
    dev.screencap(s.local / 'before.png')
    lit, h = picture_lit(s.local / 'before.png')
    print('           before: %d of %d rows lit down the middle' % (lit, h), flush=True)
    for name, what, do, undo in LIFECYCLE:
        if not dev.online():
            checks.add('lifecycle', False, '%s: the device went away' % name)
            break
        ticks = count(lines(), '[SNAP-TICK]')
        mark = len(lines())
        for c in do:
            dev.sh(c)
        time.sleep(6)
        during = dev.alive()
        dev.screencap(s.local / ('%s-during.png' % name))
        for c in undo:
            if c == '@start':
                dev.start(extras)
            else:
                dev.sh(c)
        # The game's logic must step on: two new tick reports, 240 frames.
        t1 = time.time()
        while time.time() - t1 < 90 and count(lines(), '[SNAP-TICK]') < ticks + 2:
            if not dev.alive():
                break
            time.sleep(3)
        stepped = count(lines(), '[SNAP-TICK]') - ticks
        time.sleep(2)
        dev.screencap(s.local / ('%s-after.png' % name))
        lit, h = picture_lit(s.local / ('%s-after.png' % name))
        alive = dev.alive()
        said = [l for l in lines()[mark:] if l.startswith(('[SNAP] surface', '[SNAP-RT64]', '[SNAP-AV]', '[SNAP-HANG]',
                                                          '[SNAP-LIFE]', '[SNAP-D3D12]', '[SNAP-VK'))
                or 'VK_ERROR' in l or 'surface lost' in l.lower()]
        ok = alive and stepped >= 2 and lit >= h // 2
        checks.add('lifecycle', ok, '%s: running during %s, after %s; %d new tick reports in %.0f s; %d of %d rows lit after%s'
                   % (what, during, alive, stepped, time.time() - t1, lit, h,
                      ('; the log: ' + ' | '.join(said[:4])) if said else ''))
        if not alive:
            break
    s.lines = lines()
    s.took = time.time() - t0
    s.met = True
    s.ended = not dev.alive()
    s.tag = ''
    dev.stop()
    dev.sh('cmd window user-rotation free')
    (s.local / 'snap64.log').write_text('\n'.join(s.lines) + '\n', encoding='utf-8')
    dev.logcat_to(s.local / 'logcat.txt', 'all')
    dev.logcat_to(s.local / 'crash.txt', 'crash')
    s.judge_end(checks)


def overlay_places(w, h, size=100, columns=None):
    """Where Snap64TouchOverlay.java puts each button's center on a w x h
    landscape screen: the same arithmetic as its place(). columns: the two
    columns' x as the overlay logged them (it moves them clear of a camera
    cutout, which this arithmetic does not know)."""
    base = min(w, h) * 0.085 * (size / 100.0)
    picture = h * 4.0 / 3.0
    bar = (w - picture) / 2.0
    left = bar / 2.0 if bar >= 2.8 * base else 1.8 * base
    right = w - left
    if columns:
        left, right = columns
    gap = 0.95 * base
    return {'Start': (left, h * 0.13), 'C-Up': (left, h * 0.42 - gap),
            'C-Left': (left - gap, h * 0.42 + 0.25 * gap), 'C-Right': (left + gap, h * 0.42 + 0.25 * gap),
            'C-Down': (left, h * 0.42 + 1.5 * gap), 'R': (right, h * 0.13), 'Z': (right, h * 0.37),
            'A': (right, h * 0.63), 'B': (right - 0.2 * base, h * 0.87)}


def screen_size(dev):
    """The landscape screen's size, from wm size (the panel's own, portrait on a phone)."""
    m = re.search(r'(\d+)x(\d+)', dev.sh('wm size').split('Override size:')[-1])
    w, h = int(m.group(1)), int(m.group(2))
    return max(w, h), min(w, h)


def multitouch_device(dev):
    """The first screen's touch device for sendevent, when adb can be root
    (an emulator); None on a phone."""
    if 'root' not in dev.sh('id -un').strip():
        dev.run(['root'])
        time.sleep(3)
        dev.run(['wait-for-device'], timeout=60)
    if dev.sh('id -un').strip() != 'root':
        return None
    listing = dev.sh('getevent -p 2>/dev/null')
    m = re.search(r'add device \d+: (/dev/input/event\d+)\s+name:\s+"virtio_input_multi_touch_1"', listing)
    return m.group(1) if m else None


def panel_raw(w, h, x, y):
    """A landscape point as the emulator's multi-touch screen takes it: its
    own axes are the portrait panel's, 0..32767; the picture is the panel
    turned."""
    return int((h - y) / h * 32767), int(x / w * 32767)


def two_finger_press(dev, node, w, h, hold, tap):
    """Holds one button and taps another with a second finger (sendevent on
    the emulator's multi-touch screen)."""
    lines = []

    def ev(t, c, v):
        lines.append('sendevent %s %d %d %d' % (node, t, c, v))
    (x0, y0), (x1, y1) = panel_raw(w, h, *hold), panel_raw(w, h, *tap)
    ev(3, 47, 0); ev(3, 57, 501); ev(3, 53, x0); ev(3, 54, y0); ev(3, 58, 60); ev(1, 330, 1); ev(0, 0, 0)
    lines.append('sleep 1.5')
    ev(3, 47, 1); ev(3, 57, 502); ev(3, 53, x1); ev(3, 54, y1); ev(3, 58, 60); ev(0, 0, 0)
    lines.append('sleep 0.3')
    ev(3, 47, 1); ev(3, 57, -1); ev(0, 0, 0)
    lines.append('sleep 0.8')
    ev(3, 47, 0); ev(3, 57, -1); ev(1, 330, 0); ev(0, 0, 0)
    dev.sh('; '.join(lines))


def grip_and_drag(dev, node, w, h, grip, start, end, nudge):
    """A finger grips (rests) first; a second one touches later, drags from
    start to end in ten moves and stays down; then the gripping finger moves
    by nudge in five moves and lifts; then the second lifts."""
    lines = []

    def ev(t, c, v):
        lines.append('sendevent %s %d %d %d' % (node, t, c, v))
    x0, y0 = panel_raw(w, h, *grip)
    ev(3, 47, 0); ev(3, 57, 601); ev(3, 53, x0); ev(3, 54, y0); ev(3, 58, 60); ev(1, 330, 1); ev(0, 0, 0)
    lines.append('sleep 0.5')
    for i in range(11):
        x, y = panel_raw(w, h, start[0] + (end[0] - start[0]) * i / 10.0, start[1] + (end[1] - start[1]) * i / 10.0)
        ev(3, 47, 1)
        if i == 0:
            ev(3, 57, 602); ev(3, 58, 60)
        ev(3, 53, x); ev(3, 54, y); ev(0, 0, 0)
        lines.append('sleep 0.05')
    lines.append('sleep 0.3')
    for i in range(1, 6):
        x, y = panel_raw(w, h, grip[0] + nudge[0] * i / 5.0, grip[1] + nudge[1] * i / 5.0)
        ev(3, 47, 0); ev(3, 53, x); ev(3, 54, y); ev(0, 0, 0)
        lines.append('sleep 0.05')
    ev(3, 47, 0); ev(3, 57, -1); ev(0, 0, 0)
    lines.append('sleep 0.3')
    ev(3, 47, 1); ev(3, 57, -1); ev(1, 330, 0); ev(0, 0, 0)
    dev.sh('; '.join(lines))


def zoom_switch_save(src, dst):
    """A copy of the save with the game's Z Button set to Switch (the
    player's flags at 0x67, bit 0x04: the decomp's more_funcs.h, zoomSwitch),
    its checksum made again by the game's own rule (tools/unlock_save.py)."""
    sys.path.insert(0, str(TOOLS))
    import unlock_save
    buf = bytearray(pathlib.Path(src).read_bytes())
    if unlock_save.checksum(buf) != bytes(buf[0:0x10]):
        raise RuntimeError('%s: its stored checksum does not match' % src)
    buf[0x67] |= 0x04
    buf[0:0x10] = unlock_save.checksum(buf)
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(bytes(buf))
    return dst


def stage_vkcheck(checks, dev, out, a):
    """The GPU's refusal (src/gpu_check.cpp), on a device that would pass:
    SNAP_VK_TEST=1.1 has the chosen device answer Vulkan 1.1. The port must
    say so in the log, show the message in Android's own dialog, and end by
    itself once the dialog's button is pressed, with no crash."""
    s = Stage(dev, out, 'vkcheck')
    s.fresh([(a.rom, 'pokemonsnap.z64')])
    log = s.folder + '/snap64.log'
    data = [b'']

    def lines():
        data[0] += dev.tail_from(log, len(data[0]))
        return text_lines(data[0])

    dev.stop()
    dev.run(['logcat', '-b', 'all', '-c'])
    dev.start({'SNAP_DATA_DIR': s.folder, 'SNAP_MUTE': '1', 'SNAP_VK_TEST': '1.1'})
    t0 = time.time()
    while time.time() - t0 < 120 and not any(l.startswith('[SNAP-VK] refused') for l in lines()):
        time.sleep(2)
    refused = next((l for l in lines() if l.startswith('[SNAP-VK] refused')), '')
    checks.add('vkcheck', 'offers Vulkan 1.1' in refused, refused or 'no "[SNAP-VK] refused" line in %.0f s' % (time.time() - t0))
    # The dialog: its words and its button, read from the screen's own
    # description of itself.
    time.sleep(3)
    dev.screencap(s.local / 'dialog.png')
    dev.sh('uiautomator dump /sdcard/snap64check_ui.xml')
    ui = (dev.cat('/sdcard/snap64check_ui.xml') or b'').decode('utf-8', errors='replace')
    dev.sh('rm -f /sdcard/snap64check_ui.xml')
    (s.local / 'dialog.xml').write_text(ui, encoding='utf-8')
    said = 'graphics driver is too old for Snap64 Recomp' in ui and 'Vulkan 1.1' in ui and 'Vulkan 1.2' in ui
    checks.add('vkcheck', said, 'the dialog says: %s' % ' / '.join(
        t for t in re.findall(r'text="([^"]+)"', ui) if t.strip())[:300] if said else 'the dialog\'s words are not on the screen')
    alive_with_box = dev.alive()
    button = re.search(r'text="OK"[^>]*bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', ui)
    if button:
        x0, y0, x1, y1 = (int(v) for v in button.groups())
        dev.sh('input tap %d %d' % ((x0 + x1) // 2, (y0 + y1) // 2))
    else:
        dev.sh('input keyevent KEYCODE_ENTER')
    t1 = time.time()
    while time.time() - t1 < 20 and dev.alive():
        time.sleep(1)
    ended = not dev.alive()
    s.lines = lines()
    s.took = time.time() - t0
    s.met = bool(refused)
    s.ended = ended
    s.tag = ''
    dev.stop()
    (s.local / 'snap64.log').write_text('\n'.join(s.lines) + '\n', encoding='utf-8')
    dev.logcat_to(s.local / 'logcat.txt', 'all')
    dev.logcat_to(s.local / 'crash.txt', 'crash')
    # RT64 names its device once it has made one; the "graphics API" line is
    # the settings', said before the check.
    renderer = [l for l in s.lines if l.startswith('Device Name:')]
    checks.add('vkcheck', alive_with_box and ended and not renderer,
               'the app waited on the dialog (%s), ended %.0f s after its %s was pressed (%s), and no renderer started%s'
               % (alive_with_box, time.time() - t1, 'OK' if button else 'Enter key', ended,
                  '' if not renderer else ': ' + renderer[0]))
    s.judge_end(checks, expect_end=True)


def ensure_root(dev):
    """adb as root, where it can be (an emulator's Google APIs image)."""
    if 'root' not in dev.sh('id -un').strip():
        dev.run(['root'])
        time.sleep(3)
        dev.run(['wait-for-device'], timeout=60)
    return dev.sh('id -un').strip() == 'root'


# The pad stage's presses, in order, and what the game must read for each
# (src/input.cpp's default bindings, a standard pad): the N64 buttons, and the
# stick's direction as (x, y) signs. Y and the pad's own jobs (the right
# shoulder's fast forward, the sticks pressed in, Back's pages) give the game
# nothing; they are checked by their log lines instead.
PAD_LAYOUT = [('LB', 'Z', 0x2000, 0, 0), ('LT', 'L', 0x0020, 0, 0), ('RT', 'R', 0x0010, 0, 0),
              ('X', 'B', 0x4000, 0, 0), ('B', 'B', 0x4000, 0, 0), ('Y', None, 0, 0, 0),
              ('DU', 'D-Up', 0x0800, 0, 1), ('DD', 'D-Down', 0x0400, 0, -1),
              ('DL', 'D-Left', 0x0200, -1, 0), ('DR', 'D-Right', 0x0100, 1, 0),
              ('RSU', 'C-Up', 0x0008, 0, 0), ('RSD', 'C-Down', 0x0004, 0, 0),
              ('RSL', 'C-Left', 0x0002, 0, 0), ('RSR', 'C-Right', 0x0001, 0, 0),
              ('LSU', 'the stick up', 0, 0, 1), ('LSD', 'the stick down', 0, 0, -1),
              ('LSL', 'the stick left', 0, -1, 0), ('LSR', 'the stick right', 0, 1, 0),
              ('A', 'A', 0x8000, 0, 0)]


def tape_runs(raw):
    """The runs of a recorded tape (SNAP_RECORD: per reading, the N64 buttons
    and the stick, 12 bytes): (buttons, x sign, y sign, readings) for each
    stretch of identical input that is not at rest."""
    runs = []
    for at in range(0, len(raw) - 11, 12):
        btn, x, y = struct.unpack_from('<H2xff', raw, at)
        sig = (btn, (x > 0.3) - (x < -0.3), (y > 0.3) - (y < -0.3))
        if sig == (0, 0, 0):
            runs.append(None)
        elif runs and runs[-1] is not None and runs[-1][:3] == sig:
            runs[-1] = sig + (runs[-1][3] + 1,)
        else:
            runs.append(sig + (1,))
    return [r for r in runs if r is not None]


def stage_pad(checks, dev, out, a):
    """A game pad, made where adb can be root (an emulator) by uinput
    (tools/android/uipad.c, --uipad): Android takes it for a pad, SDL opens
    it, and the port uses it over the emulator's own keyboard, which SDL maps
    as a pad with no stick. Every button and both sticks are pressed in turn
    with SNAP_RECORD on, and the tape must show the N64 input the default
    bindings give each; the touch controls hide at the pad's first press and
    come back at a touch; the right shoulder fast-forwards, the left stick
    pressed in slows the game, and Back opens the pages from anywhere."""
    if not a.uipad:
        print('    pad: not run (it needs --uipad, the uinput helper built for the device)', flush=True)
        return
    if not ensure_root(dev):
        print('    pad: not run (adb cannot be root here: a phone)', flush=True)
        return
    s = Stage(dev, out, 'pad')
    # Slow motion is off as shipped (the cartridge's pace); the stage turns
    # it on to see the left stick pressed in work.
    settings = pathlib.Path(out) / 'pad-settings.json'
    settings.parent.mkdir(parents=True, exist_ok=True)
    settings.write_text('{"slow_motion_speed": 2}\n', encoding='utf-8')
    s.fresh([(a.rom, 'pokemonsnap.z64'), (a.save, 'saves/pokemonsnap.bin'), (settings, 'snapsettings.json')])
    helper, fifo, said = '/data/local/tmp/snap64-uipad', '/data/local/tmp/snap64-uipad.fifo', '/data/local/tmp/snap64-uipad.out'
    dev.push(a.uipad, helper)
    dev.sh('chmod 755 %s; rm -f %s %s; mkfifo %s' % (helper, fifo, said, fifo))
    log = s.folder + '/snap64.log'
    data = [b'']

    def lines():
        data[0] += dev.tail_from(log, len(data[0]))
        return text_lines(data[0])

    def wait(pred, cap):
        t0 = time.time()
        while time.time() - t0 < cap:
            if pred(lines()):
                return True
            time.sleep(1.0)
        return False

    def send(command):
        dev.sh("echo '%s' > %s" % (command, fifo), timeout=60)

    def a_drawn(name):
        # The overlay's blue A, where the touch stage looks for it.
        try:
            from PIL import Image
            w, h = screen_size(dev)
            places = overlay_places(w, h)
            found = re.findall(r'columns at ([\d.]+) and ([\d.]+) on', dev.run(['logcat', '-d', '-s', 'Snap64Touch']))
            if found:
                places = overlay_places(w, h, columns=(float(found[-1][0]), float(found[-1][1])))
            x, y = places['A']
            rgb = Image.open(s.local / name).convert('RGB').getpixel((int(x), int(y - 0.7 * min(w, h) * 0.085 * 1.2)))
        except Exception:
            return None, None
        return rgb[2] > rgb[0] + 30, rgb

    dev.stop()
    dev.run(['logcat', '-b', 'all', '-c'])
    dev.start({'SNAP_DATA_DIR': s.folder, 'SNAP_MUTE': '1', 'SNAP_RECORD': s.folder + '/pad.inputs'})
    t0 = time.time()
    wait(lambda ls: any(l.startswith('[SNAP] fullscreen:') for l in ls), 120)
    wait(lambda ls: any('the touch controls are shown' in l for l in ls), 30)
    time.sleep(8)
    before = [l for l in lines() if l.startswith('[SNAP-Input] Opened game controller:')]
    print('           before the pad: %s' % (' | '.join(before) or 'no controller open'), flush=True)
    serial = ['-s', a.serial] if a.serial else []
    proc = subprocess.Popen([a.adb] + serial + ['shell', '%s %s > %s 2>&1' % (helper, fifo, said)],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    mark = len(lines())
    # SDL names the pad by its ids' mapping (an Xbox 360 controller's).
    opened = wait(lambda ls: any(l.startswith('[SNAP-Input] pad ids 045E:028E') for l in ls[mark:]), 30)
    after = lines()[mark:]
    ids = next((l for l in after if l.startswith('[SNAP-Input] pad ids')), '')
    layout = next((l for l in after if l.startswith('[SNAP-Input] pad layout:')), '')
    switched = next((l for l in after if 'is the pad now' in l), '')
    checks.add('pad', opened and '045E:028E' in ids and 'standard' in layout,
               'the made pad is opened%s: %s; %s' % (' in place of the open one' if switched else '',
                                                     ids[len('[SNAP-Input] '):] or 'no ids line',
                                                     layout[len('[SNAP-Input] '):] or 'no layout line'))
    if switched:
        print('           %s' % switched, flush=True)
    time.sleep(2)
    dev.screencap(s.local / 'pad-attached.png')
    drawn_attached, rgb_attached = a_drawn('pad-attached.png')
    # Start through the intro to the title, then every control in turn.
    mark = len(lines())
    for _ in range(10):
        send('press START 400')
        time.sleep(1.5)
        if wait(lambda ls: any('menu font harvested' in l for l in ls), 3):
            break
    title = any('menu font harvested' in l for l in lines())
    hid = any('touch controls are hidden' in l for l in lines()[mark:])
    dev.screencap(s.local / 'pad-used.png')
    drawn_used, rgb_used = a_drawn('pad-used.png')
    checks.add('pad', hid and drawn_used is False and drawn_attached is True,
               'the touch controls stay while the pad is only attached (A at %s), and hide at its first press '
               '(A at %s)' % (rgb_attached, rgb_used))
    checks.add('pad', title, 'Start on the pad goes through the intro to the title')
    time.sleep(4)
    for name, _, _, _, _ in PAD_LAYOUT:
        send('press %s 500' % name)
        time.sleep(1.2)
    time.sleep(2)
    # The pad's own jobs, by their log lines.
    mark = len(lines())
    send('press RB 1500')
    time.sleep(2.0)
    send('press LS 1500')
    time.sleep(2.5)
    jobs = lines()[mark:]
    fast = any(l.startswith('[SNAP] fast forward:') and 'held' in l for l in jobs)
    slow = any(l.startswith('[SNAP] slow motion:') and 'held' in l for l in jobs)
    checks.add('pad', fast and slow, 'the right shoulder fast-forwards (%s) and the left stick pressed in slows '
               'the game (%s) while held' % ('said' if fast else 'NOT said', 'said' if slow else 'NOT said'))
    mark = len(lines())
    send('press BACK 300')
    pages = wait(lambda ls: any('the pages from here: opened' in l for l in ls[mark:]), 10)
    time.sleep(2)
    dev.screencap(s.local / 'pad-pages.png')
    send('press BACK 300')
    time.sleep(3)
    checks.add('pad', pages, 'Back opens the pages from anywhere%s' %
               ('' if pages else ' (no "the pages from here" line)'))
    # A touch brings the controls back.
    mark = len(lines())
    w, h = screen_size(dev)
    dev.sh('input tap %d %d' % (w * 0.5, h * 0.5))
    back = wait(lambda ls: any('touch controls are shown' in l for l in ls[mark:]), 5)
    time.sleep(0.6)
    dev.screencap(s.local / 'pad-touched.png')
    drawn, rgb = a_drawn('pad-touched.png')
    checks.add('pad', back and drawn is True, 'a touch shows the controls again (A at %s)' % (rgb,))
    send('quit')
    try:
        proc.wait(timeout=30)
    except subprocess.TimeoutExpired:
        proc.kill()
    s.lines = lines()
    s.took = time.time() - t0
    s.met = True
    s.ended = not dev.alive()
    s.tag = ''
    dev.stop()
    time.sleep(1.0)
    (s.local / 'snap64.log').write_text('\n'.join(lines()) + '\n', encoding='utf-8')
    (s.local / 'uipad.out').write_bytes(dev.cat(said) or b'')
    tape = dev.cat(s.folder + '/pad.inputs') or b''
    (s.local / 'pad.inputs').write_bytes(tape)
    dev.logcat_to(s.local / 'logcat.txt', 'all')
    dev.logcat_to(s.local / 'crash.txt', 'crash')
    # The tape: from the first Start to the A at the end of the layout, one
    # run of input for each press but Y's, each the N64 input it must be.
    runs = tape_runs(tape)
    want = [(btn, x, y) for _, n64, btn, x, y in PAD_LAYOUT if n64]
    # The layout follows the last Start that went through the intro: the
    # Start run just before the first press of the layout.
    starts = [i for i, r in enumerate(runs) if r[:3] == (0x1000, 0, 0)]
    led = [i for i in starts if i + 1 < len(runs) and runs[i + 1][:3] == want[0]]
    got = []
    if starts:
        first = led[0] if led else starts[0]
        got = [r[:3] for r in runs[first + 1:first + 1 + len(want)]]
    names = [n for n, n64, _, _, _ in PAD_LAYOUT if n64]
    wrong = ['%s gave %s' % (names[i], '%04X %+d %+d' % got[i] if i < len(got) else 'nothing')
             for i in range(len(want)) if i >= len(got) or got[i] != want[i]]
    checks.add('pad', bool(starts) and not wrong,
               'the tape: %d readings; after Start, %s' % (len(tape) // 12,
                                                            '%d presses each read as its N64 input (%s)' % (
                                                                len(want), ', '.join('%s %s' % (n, m) for n, m, _, _, _ in
                                                                                     PAD_LAYOUT if m))
                                                            if not wrong else 'WRONG: ' + '; '.join(wrong[:6])))
    s.judge_end(checks)


def stage_touch(checks, dev, out, a, switch=False, gyro=False, wide=False):
    """The touch controls by touch alone, from the intro to a course: the
    overlay's Start, a tap on the title's Continue (the pointer), A for Oak,
    taps on Go to Course and the Beach, A for Let's go; in the course the
    C buttons show, a drag aims, quick taps are read, a key hides the
    controls and a touch shows them. Z, in the game's Hold mode, is held by
    a tap and let go by the next: the game zooms in and out and Z is drawn
    lit between. Where adb can be root (an emulator) Z is tapped while a
    second finger presses A, and a finger resting on the picture neither
    keeps the aim from one that moves nor aims while it does; the first
    course's hint shows and goes; A zoomed in asks for the firmer feedback.
    switch (the touchswitch stage): the save's Z Button is Switch and the
    buttons are at touch_size 50: Start is pressed outside its drawn circle
    but inside its 48 dp square, a tap on Z is a plain press that zooms in
    or out, and a tap at C-Up's center is C-Up's though a neighbor's square
    covers it.
    gyro (the gyro stage): Gyro Aim on in the run's settings; zoomed in, the
    emulator's gyroscope turns the phone left and then raises it, each for
    a second, with its accelerometer saying the screen's up is up: the view
    must turn left and then up, by the angle the readings made, and a still
    phone for three seconds turns nothing. Each step waits for the game's
    own line; the save is the tapes' (the Beach is open)."""
    s = Stage(dev, out, 'gyro' if gyro else ('touchswitch' if switch else ('touchwide' if wide else 'touch')))
    save = a.save
    if switch:
        save = zoom_switch_save(a.save, pathlib.Path(out) / 'touchswitch-save.bin')
    files = [(a.rom, 'pokemonsnap.z64'), (save, 'saves/pokemonsnap.bin')]
    if gyro:
        settings = pathlib.Path(out) / 'gyro-settings.json'
        settings.parent.mkdir(parents=True, exist_ok=True)
        settings.write_text('{"gyro_aim": 1}\n', encoding='utf-8')
        files.append((settings, 'snapsettings.json'))
    elif wide:
        settings = pathlib.Path(out) / 'touchwide-settings.json'
        settings.parent.mkdir(parents=True, exist_ok=True)
        settings.write_text('{"widescreen": true, "wide_hud": true}\n', encoding='utf-8')
        files.append((settings, 'snapsettings.json'))
    elif switch:
        settings = pathlib.Path(out) / 'touchswitch-settings.json'
        settings.parent.mkdir(parents=True, exist_ok=True)
        settings.write_text('{"touch_size": 50}\n', encoding='utf-8')
        files.append((settings, 'snapsettings.json'))
    elif a.touch_settings:
        # A settings file of the caller's (for example the course in widescreen).
        settings = pathlib.Path(out) / 'touch-settings.json'
        settings.parent.mkdir(parents=True, exist_ok=True)
        settings.write_text(a.touch_settings + '\n', encoding='utf-8')
        files.append((settings, 'snapsettings.json'))
    s.fresh(files)
    w, h = screen_size(dev)
    size = 50 if switch else 100
    places = overlay_places(w, h, size)
    # Every button takes touches over 48 dp at the least (the overlay's put).
    m = re.search(r'(\d+)', dev.sh('wm density').split('Override density:')[-1])
    floor = 48.0 * (int(m.group(1)) if m else 160) / 160.0
    picture = h * 4.0 / 3.0
    bar = (w - picture) / 2.0

    def game(gx, gy):   # the game's own 320 x 240 pixels, on the screen
        return bar + gx / 320.0 * picture, gy / 240.0 * h
    log = s.folder + '/snap64.log'
    data = [b'']

    def lines():
        data[0] += dev.tail_from(log, len(data[0]))
        return text_lines(data[0])

    def wait(pred, cap):
        t0 = time.time()
        while time.time() - t0 < cap:
            if pred(lines()):
                return True
            time.sleep(1.0)
        return False

    def press(x, y, ms=250):
        dev.sh('input swipe %d %d %d %d %d' % (x, y, x, y, ms))

    def step(name, ok, detail=''):
        checks.add(s.name, ok, name + (': ' + detail if detail else ''))
        return ok

    def held_lines(since):
        return [l.split('buttons held: ', 1)[1] for l in lines()[since:] if 'buttons held: ' in l]

    dev.stop()
    dev.run(['logcat', '-b', 'all', '-c'])
    extras = {'SNAP_DATA_DIR': s.folder, 'SNAP_MUTE': '1'}
    if gyro:
        extras['SNAP_GYRO_DEBUG'] = '1'
    if not (switch or gyro or wide):
        extras['SNAP_TOUCH_HINT'] = 'show'      # the first course's hint, whatever this install has seen
    dev.start(extras)
    t0 = time.time()
    ok = step('the controls show at the start',
              wait(lambda ls: any('the touch controls are shown' in l for l in ls), 120))
    wait(lambda ls: any(l.startswith('[SNAP] fullscreen:') for l in ls), 60)
    time.sleep(12)
    # The columns where the overlay put them (clear of a camera cutout).
    found = re.findall(r'columns at ([\d.]+) and ([\d.]+) on', dev.run(['logcat', '-d', '-s', 'Snap64Touch']))
    if found:
        places = overlay_places(w, h, size, columns=(float(found[-1][0]), float(found[-1][1])))
        print('           the overlay\'s columns: x %s and %s' % found[-1], flush=True)
    dev.screencap(s.local / 'menu.png')
    # The overlay drawn: A blue, B green and Start red where the layout says.
    g = None
    try:
        from PIL import Image
        g = Image.open(s.local / 'menu.png').convert('RGB')
    except Exception:
        pass
    base = min(w, h) * 0.085 * size / 100.0
    scale = {'A': 1.2, 'B': 0.9, 'Start': 0.62}

    def tint(img, name):
        # Inside the circle, above its letter.
        x, y = places[name]
        return img.getpixel((int(x), int(y - 0.7 * base * scale[name])))

    def a_drawn(name):
        # Whether the overlay's blue A is on a screenshot just taken.
        try:
            from PIL import Image
            rgb = tint(Image.open(s.local / name).convert('RGB'), 'A')
        except Exception:
            return None, None
        return rgb[2] > rgb[0] + 30, rgb

    def z_ring(name, key='Z', scale=0.9):
        # A held button's second ring (drawn while it is on, at 1.1 of its
        # radius, outside the first): the brightest gray from a pixel and a
        # half past the first ring's stroke to 1.14 radii, to the right, the
        # left, below and above. In the black bar, off is dark.
        try:
            from PIL import Image
            img = Image.open(s.local / name).convert('RGB')
        except Exception:
            return None, None
        zx, zy = places[key]
        r = base * scale
        k0 = r + max(2.0, r * 0.07) / 2.0 + 1.5
        seen = []
        for ux, uy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            best = 0
            k = k0
            while k <= 1.14 * r:
                p = img.getpixel((int(round(zx + ux * k)), int(round(zy + uy * k))))
                best = max(best, (299 * p[0] + 587 * p[1] + 114 * p[2]) // 1000)
                k += 0.5
            seen.append(best)
        if min(seen) >= 80:
            return True, seen
        if max(seen) < 50:
            return False, seen
        return None, seen

    def tap(x, y):
        dev.sh('input tap %d %d' % (x, y))

    def reach_course():
        # Start through the intro to the title's menu. At size 50 it is
        # pressed to the right of its drawn circle, inside its 48 dp square:
        # without the floor that is the game's, not Start's.
        sx, sy = places['Start']
        name = 'Start, pressed on the overlay, reaches the title'
        reach = 1.15 * 0.62 * base             # the square's half without the floor
        if switch and (floor / 2.0 > reach + 8):
            off = (reach + floor / 2.0) / 2.0
            sx += off
            name = ('Start, pressed %.0f px from its center (its circle %.0f, its square without the floor %.0f, '
                    'with it %.0f), reaches the title' % (off, 0.62 * base, reach, floor / 2.0))
        started = False
        for _ in range(15):
            press(sx, sy, ms=300)
            if wait(lambda ls: any('menu font harvested' in l for l in ls), 4):
                started = True
                break
        step(name, started)
        time.sleep(3)
        press(sx, sy, ms=300)
        time.sleep(3)
        # The title's Continue, tapped where the game draws it: the pointer.
        for _ in range(3):
            tap(*game(160, 159))
            if wait(lambda ls: any(l.startswith('[SNAP-SAVE] playing save file') for l in ls), 5):
                break
        step('a tap on the title\'s Continue loads the save',
             any(l.startswith('[SNAP-SAVE] playing save file') for l in lines()))
        time.sleep(8)
        press(*places['A'])                 # Oak's welcome
        time.sleep(3)
        tap(*game(53, 35))                  # Go to Course
        time.sleep(6)
        tap(*game(53, 35))                  # the Beach
        time.sleep(5)
        press(*places['A'])                 # Let's go!
        return step('taps on Go to Course and the Beach, and A, start the course',
                    wait(lambda ls: any('a course runs' in l for l in ls), 90))

    def zoom_tap(want_held, want_zoom):
        # A tap on Z; then the buttons the overlay held, the game's zoom line
        # and Z's ring on a screenshot.
        mark = len(lines())
        tap(*places['Z'])
        zoomed = wait(lambda ls: any(('the game zoomed %s' % want_zoom) in l for l in ls[mark:]), 10)
        time.sleep(1.0)
        held = held_lines(mark)
        shot = 'z-%d.png' % len(lines())
        dev.screencap(s.local / shot)
        lit, seen = z_ring(shot)
        return zoomed and (held == want_held), lit, '%s; zoomed %s: %s; ring %s (%s)' % (
            ' / '.join(held) or 'no buttons line', want_zoom, zoomed, seen, shot)

    def shutters():
        # The firmer feel asked for (CONFIRM, 16) in the vibrator's record,
        # and the native side's line for each photo it handed the overlay.
        firm = sum(1 for l in dev.sh('dumpsys vibrator_manager').splitlines()
                   if (PKG in l) and ('performHapticFeedback(constant=16)' in l))
        return firm, sum(1 for l in lines() if 'the game took a photo: the shutter is felt' in l)

    def mode_named(word):
        named = [l for l in lines() if "the game's Z Button is" in l]
        return bool(named) and (word in named[-1]), (named[-1] if named else 'no line')
    if g is not None:
        a_rgb, b_rgb, s_rgb = tint(g, 'A'), tint(g, 'B'), tint(g, 'Start')
        drawn = (a_rgb[2] > a_rgb[0] + 30) and (b_rgb[1] > b_rgb[0] + 20) and (s_rgb[0] > s_rgb[2] + 30)
        step('A, B and Start drawn at their places', drawn, 'A %s, B %s, Start %s' % (a_rgb, b_rgb, s_rgb))
    course = reach_course()

    def film_dark(name):
        # Near-black pixels (the film strip's edges and holes) where the
        # counter stands at the 4:3 place, and in the wide picture's top
        # corner, under R: on the emulator's 2400 x 1080, the counter is 282
        # pixels long, 1637 to 1919 at its place, 2118 to 2400 in the corner.
        from PIL import Image
        img = Image.open(s.local / name).convert('RGB')
        edge = bar + picture

        def count(x0, x1):
            n = 0
            for y in range(int(0.10 * h), int(0.165 * h)):
                for x in range(int(x0), int(x1)):
                    if max(img.getpixel((x, y))) < 45:
                        n += 1
            return n
        return count(edge - 0.104 * w, edge - 0.008 * w), count(w - 0.11 * w, w - 0.005 * w)
    if course and wide:
        time.sleep(30)
        dev.screencap(s.local / 'wide-shown.png')
        at43, corner = film_dark('wide-shown.png')
        step('the controls shown, the film counter keeps its 4:3 place, clear of R', (at43 >= 2000) and (corner <= 200),
             '%d dark pixels at the place, %d in R\'s corner' % (at43, corner))
        mark = len(lines())
        dev.sh('input keyboard keyevent KEYCODE_Q')
        hid = wait(lambda ls: any('touch controls are hidden' in l for l in ls[mark:]), 5)
        time.sleep(1.0)
        dev.screencap(s.local / 'wide-hidden.png')
        at43, corner = film_dark('wide-hidden.png')
        step('the controls hidden, Wide HUD puts the counter in the wide corner', hid and (corner >= 2000) and (at43 <= 200),
             '%d dark pixels at the 4:3 place, %d in the corner' % (at43, corner))
        mark = len(lines())
        tap(w * 0.5, h * 0.5)
        back = wait(lambda ls: any('touch controls are shown' in l for l in ls[mark:]), 5)
        time.sleep(1.0)
        dev.screencap(s.local / 'wide-shown-again.png')
        at43, corner = film_dark('wide-shown-again.png')
        step('shown again, the counter is back at its 4:3 place', back and (at43 >= 2000) and (corner <= 200),
             '%d dark pixels at the place, %d in the corner' % (at43, corner))
    elif course and gyro:
        time.sleep(30)
        on = [l for l in lines() if "the phone's own sensor is on" in l]
        step('Gyro Aim on opens the phone\'s own gyroscope', bool(on), on[-1] if on else 'no line')
        # The device's x axis is the screen's up in SDL's LANDSCAPE (the
        # phone turned left), its down in LANDSCAPE_FLIPPED. The
        # accelerometer is made to say so: the phone held upright.
        up = -1.0 if (on and 'turned right' in on[-1]) else 1.0
        dev.run(['emu', 'sensor', 'set', 'acceleration', '%.2f:0:0' % (9.81 * up)])

        def gyro_lines():
            return [l for l in lines() if l.startswith('[SNAP-GYRO] phone:')]

        # The tapes' save plays the Beach tutorial, which holds the view
        # (input off, the stick's process paused) until three photos are
        # taken (app_level/player.c, updateTutorialMain), and while a Pokemon
        # is in focus waits for Z: Z (held by its tap), A three times, Z let
        # go, until the game lets go of the view, as the debug line reads it.
        free = False
        for _ in range(5):
            tap(*places['Z'])
            time.sleep(2.0)
            for _ in range(3):
                tap(*places['A'])
                time.sleep(1.2)
            tap(*places['Z'])
            time.sleep(2.0)
            if 'input off 0, stick runs 1' in (gyro_lines() or [''])[-1]:
                free = True
                break
        step('the game lets go of the view (the Beach tutorial\'s three photos taken)', free,
             (gyro_lines() or [''])[-1].split('game: ', 1)[-1])
        zoom_tap(['Z'], 'in')       # zoomed in, the yaw has no facing to fold into

        def gyro_nums(line):
            m = re.search(r'made yaw=(-?[\d.]+) pitch=(-?[\d.]+) rad.* the view yaw=(-?[\d.]+) pitch=(-?[\d.]+)',
                          line or '')
            return tuple(float(x) for x in m.groups()) if m else None

        def turn(rates, hold=1.0, settle=2.0):
            # The rates (rad/s, the device's own axes) for `hold` seconds;
            # then what the readings made and the view did between the last
            # debug line before and the last one `settle` seconds after.
            before = (gyro_lines() or [''])[-1]
            dev.run(['emu', 'sensor', 'set', 'gyroscope', '%g:%g:%g' % rates])
            t1 = time.time()
            time.sleep(hold)
            dev.run(['emu', 'sensor', 'set', 'gyroscope', '0:0:0'])
            took = time.time() - t1
            time.sleep(settle)
            after = (gyro_lines() or [''])[-1]
            b, e = gyro_nums(before), gyro_nums(after)
            if (b is None) or (e is None):
                return took, None, before, after
            dyaw = e[2] - b[2]
            dyaw = (dyaw + math.pi) % (2.0 * math.pi) - math.pi   # the yaw wraps at a full turn
            return took, (e[0] - b[0], e[1] - b[1], dyaw, e[3] - b[3]), before, after

        def near(view, made, want):
            return (abs(view - made) <= 0.1 * abs(made) + 0.01) and (0.65 * abs(want) <= abs(made) <= 1.35 * abs(want))
        took, d, b, e = turn((0.5 * up, 0.0, 0.0))
        step('turning the phone left turns the view left, as far as its readings say',
             (d is not None) and (d[2] < 0) and near(d[2], d[0], 0.5 * took),
             ('the readings made %.3f rad, the view turned %.3f; %.3f expected over %.2f s' % (d[0], d[2], -0.5 * took, took))
             if d else 'no debug lines: %r / %r' % (b, e))
        took, d, b, e = turn((0.0, -0.2 * up, 0.0))
        step('raising the phone raises the view, as far as its readings say',
             (d is not None) and (d[3] > 0) and near(d[3], d[1], 0.2 * took),
             ('the readings made %.3f rad, the view rose %.3f; %.3f expected over %.2f s' % (d[1], d[3], 0.2 * took, took))
             if d else 'no debug lines: %r / %r' % (b, e))
        before = (gyro_lines() or [''])[-1]
        time.sleep(3.0)
        b, e = gyro_nums(before), gyro_nums((gyro_lines() or [''])[-1])
        still = (b is not None) and (e is not None) and all(abs(e[i] - b[i]) < 0.002 for i in range(4))
        step('a still phone for three seconds turns nothing', still,
             'made %+.4f %+.4f, the view %+.4f %+.4f' % tuple(e[i] - b[i] for i in range(4)) if (b and e) else 'no lines')
        dev.run(['emu', 'sensor', 'set', 'acceleration', '0:9.77631:0.812349'])   # the emulator's own
    elif course and switch:
        time.sleep(30)
        ok, named = mode_named('Switch')
        step('the log names the game\'s Z Button as Switch', ok, named)
        # A tap is a press and a release; the game toggles the zoom at the
        # press, and Z is lit by the game's zoom alone.
        ok, lit, said = zoom_tap(['Z', 'none'], 'in')
        step('a tap on Z is a plain press, the game zooms in, and Z is drawn lit', ok and (lit is True), said)
        ok, lit, said = zoom_tap(['Z', 'none'], 'out')
        step('the next tap zooms out, and Z is drawn plain', ok and (lit is False), said)
        # At this size C-Right's square covers C-Up's center (its own square
        # is drawn later, on top): the nearer button must take the tap.
        mark = len(lines())
        tap(*places['C-Up'])
        wait(lambda ls: any('buttons held: none' in l for l in ls[mark:]), 5)
        held = held_lines(mark)
        step('a tap at C-Up\'s center is C-Up\'s, though C-Right\'s 48 dp square covers it', held == ['C-Up', 'none'],
             ' / '.join(held) or 'no buttons line')
    elif course:
        # The first course's hint: over the picture's lower middle, then
        # gone after five seconds (the overlay's log lines).
        time.sleep(1.0)
        dev.sh('uiautomator dump %s/ui.xml > /dev/null 2>&1' % s.folder)
        ui = dev.sh('cat %s/ui.xml' % s.folder)
        hm = re.search(r'text="Drag to aim\. Tap Z to zoom\. A takes the photo\."[^>]*bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', ui)
        where = ''
        placed = False
        if hm:
            x1, y1, x2, y2 = (int(v) for v in hm.groups())
            placed = (abs((x1 + x2) / 2.0 - w / 2.0) < 0.05 * w) and ((y1 + y2) / 2.0 > 0.6 * h)
            where = 'at %d,%d to %d,%d' % (x1, y1, x2, y2)
        time.sleep(7.0)
        said = dev.run(['logcat', '-d', '-s', 'Snap64Touch'])
        step('the first course\'s hint shows over the picture\'s lower middle, then goes',
             placed and ('hint is shown' in said) and ('hint is gone' in said),
             '%s; shown %s, gone %s' % (where or 'not on the screen', 'hint is shown' in said, 'hint is gone' in said))
        time.sleep(22)
        dev.screencap(s.local / 'course.png')
        mark = len(lines())
        dev.sh('input swipe %d %d %d %d 600' % (w * 0.42, h * 0.5, w * 0.62, h * 0.45))
        step('a drag in the course aims', wait(lambda ls: any('a drag aimed' in l for l in ls[mark:]), 10),
             next((l for l in lines()[mark:] if 'a drag aimed' in l), ''))
        # Quick taps (adb's tap is down and up within a few milliseconds,
        # less than one of the game's frames): every one must reach the game.
        mark = len(lines())
        for _ in range(20):
            dev.sh('input tap %d %d' % places['A'])
            time.sleep(0.25)
        time.sleep(2)
        read = sum(1 for l in lines()[mark:] if 'the game read a press: A' in l)
        step('20 quick taps on A, each read by the game', read == 20, '%d of 20 read' % read)
        # Each press and release is handed to the phone's vibrator as touch
        # feedback (the system's record of it; the emulator has no motor).
        mine = [l for l in dev.sh('dumpsys vibrator_manager').splitlines() if (PKG in l) and ('usage: TOUCH' in l)]
        pressed = sum(1 for l in mine if 'performHapticFeedback(constant=1)' in l)
        released = sum(1 for l in mine if 'performHapticFeedback(constant=8)' in l)
        firm, shot = shutters()
        step('A unzoomed takes no photo, and no shutter is felt', (firm == 0) and (shot == 0),
             '%d firmer feedbacks, %d photos handed over' % (firm, shot))
        step('the buttons\' presses and releases reach the phone\'s vibrator as touch feedback',
             (pressed >= 10) and (released >= 10),
             '%d presses and %d releases in its record (touch feedback setting: %s)'
             % (pressed, released, dev.sh('settings get system haptic_feedback_enabled').strip()))
        # A key hides the automatic controls; a touch shows them again.
        mark = len(lines())
        dev.sh('input keyboard keyevent KEYCODE_Q')
        hid = wait(lambda ls: any('touch controls are hidden' in l for l in ls[mark:]), 5)
        time.sleep(0.6)
        dev.screencap(s.local / 'hidden.png')
        drawn, rgb = a_drawn('hidden.png')
        step('a key press hides the controls', hid and (drawn is False), 'A at %s' % (rgb,))
        mark = len(lines())
        dev.sh('input tap %d %d' % (w * 0.5, h * 0.5))
        back = wait(lambda ls: any('touch controls are shown' in l for l in ls[mark:]), 5)
        time.sleep(0.6)
        dev.screencap(s.local / 'shown-again.png')
        drawn, rgb = a_drawn('shown-again.png')
        step('a touch shows them again', back and (drawn is True), 'A at %s' % (rgb,))
        ok, named = mode_named('Hold')
        step('the log names the game\'s Z Button as Hold', ok, named)
        opened = [l for l in lines() if "the phone's own sensor" in l]
        step('with Gyro Aim off (the default) the phone\'s gyroscope is never opened', not opened,
             opened[0] if opened else '')
        node = multitouch_device(dev)
        if node is None:
            print('           two fingers at once: not tried (adb cannot be root here)', flush=True)
        else:
            # A thumb holds Z for 2.6 s while another finger presses A: a
            # hold, as on the N64, so Z is let go at its lift.
            mark = len(lines())
            two_finger_press(dev, node, w, h, places['Z'], places['A'])
            step('Z held and A pressed by a second finger', wait(lambda ls: any('buttons held: A Z' in l
                                                                                for l in ls[mark:]), 10))
            zoomed = wait(lambda ls: any('the game zoomed in' in l for l in ls[mark:]), 10)
            unzoomed = wait(lambda ls: any('the game zoomed out' in l for l in ls[mark:]), 10)
            held = held_lines(mark)
            step('Z held 2.6 s is let go at its lift, and the game zooms in and out (a hold)',
                 (held == ['Z', 'A Z', 'Z', 'none']) and zoomed and unzoomed,
                 '%s; zoomed in %s, out %s' % (' / '.join(held), zoomed, unzoomed))
        # A tap on Z holds it; A then takes a photo, and the shutter is felt
        # when the game takes it, once a photo; the next tap lets Z go.
        ok, lit, said = zoom_tap(['Z'], 'in')
        step('a tap on Z holds it, the game zooms in, and Z is drawn lit', ok and (lit is True), said)
        firm0, shot0 = shutters()
        tap(*places['A'])
        time.sleep(2.0)
        firm1, shot1 = shutters()
        step('A zoomed in takes a photo, and the shutter is felt once a photo, when the game takes it',
             (shot1 - shot0 >= 1) and (firm1 - firm0 == shot1 - shot0),
             '%d photos handed over, %d firmer feedbacks' % (shot1 - shot0, firm1 - firm0))
        ok, lit, said = zoom_tap(['none'], 'out')
        step('the next tap on Z lets it go, the game zooms out, and Z is drawn plain', ok and (lit is False), said)
        # A press of 0.8 s is a hold: Z zooms only while it is down.
        mark = len(lines())
        press(*places['Z'], ms=800)
        unzoomed = wait(lambda ls: any('the game zoomed out' in l for l in ls[mark:]), 10)
        held = held_lines(mark)
        zoomed = any('the game zoomed in' in l for l in lines()[mark:])
        step('Z pressed 0.8 s zooms only while held, as on the N64', (held == ['Z', 'none']) and zoomed and unzoomed,
             '%s; zoomed in %s, out %s' % (' / '.join(held), zoomed, unzoomed))
        # R, the dash, the same way: a tap holds it (lit), the next lets go;
        # a press of 0.8 s is a hold.
        mark = len(lines())
        tap(*places['R'])
        time.sleep(1.0)
        held = held_lines(mark)
        dev.screencap(s.local / 'r-held.png')
        lit, seen = z_ring('r-held.png', 'R', 0.72)
        step('a tap on R holds the dash, and R is drawn lit', (held == ['R']) and (lit is True),
             '%s; ring %s' % (' / '.join(held) or 'no buttons line', seen))
        mark = len(lines())
        tap(*places['R'])
        time.sleep(1.0)
        held = held_lines(mark)
        dev.screencap(s.local / 'r-let-go.png')
        lit, seen = z_ring('r-let-go.png', 'R', 0.72)
        step('the next tap on R lets it go, and R is drawn plain', (held == ['none']) and (lit is False),
             '%s; ring %s' % (' / '.join(held) or 'no buttons line', seen))
        mark = len(lines())
        press(*places['R'], ms=800)
        time.sleep(1.0)
        held = held_lines(mark)
        step('R pressed 0.8 s dashes only while held', held == ['R', 'none'], ' / '.join(held) or 'no buttons line')
        # Held by a tap, Z lets go when a key hides the controls, and that
        # release does not show them again.
        zoom_tap(['Z'], 'in')
        mark = len(lines())
        dev.sh('input keyboard keyevent KEYCODE_Q')
        hid = wait(lambda ls: any('touch controls are hidden' in l for l in ls[mark:]), 5)
        zoomed_out = wait(lambda ls: any('the game zoomed out' in l for l in ls[mark:]), 10)
        time.sleep(2.0)
        held = held_lines(mark)
        shown_again = any('touch controls are shown' in l for l in lines()[mark:])
        step('a key press with Z held hides the controls, lets Z go, and they stay hidden',
             hid and (held == ['none']) and zoomed_out and not shown_again,
             'hidden %s; %s; zoomed out %s; shown again %s' % (hid, ' / '.join(held) or 'no buttons line',
                                                               zoomed_out, shown_again))
        mark = len(lines())
        tap(w * 0.5, h * 0.5)
        wait(lambda ls: any('touch controls are shown' in l for l in ls[mark:]), 5)
        if node is not None:
            # A finger grips the picture and rests; a second, down later,
            # drags across it and stays; then the gripping finger moves down
            # a little and lifts. The resting grip must not keep the aim from
            # the finger that moves, and its own later motion must not aim
            # while the other does: the drag line holds the second finger's
            # motion alone, the grip's is named as not aiming.
            time.sleep(1.0)
            mark = len(lines())
            grip_and_drag(dev, node, w, h, (w * 0.35, h * 0.6), (w * 0.5, h * 0.4), (w * 0.65, h * 0.4),
                          (0, h * 0.1))
            wait(lambda ls: any('a drag aimed' in l for l in ls[mark:]), 10)
            aimed = [l for l in lines()[mark:] if 'a drag aimed' in l]
            other = [l for l in lines()[mark:] if 'another finger moved' in l]

            def by(line, prefix):
                m = re.search(prefix + r':? (-?\d+) by (-?\d+)', line) if line else None
                return (int(m.group(1)), int(m.group(2))) if m else None
            mover = by(aimed[-1] if aimed else '', 'a drag aimed')
            grip = by(other[-1] if other else '', 'another finger moved')
            step('a resting finger does not keep the aim from one that moves, and does not aim while it does',
                 (len(aimed) == 1) and (mover is not None) and (mover[0] >= 50) and (abs(mover[1]) <= 2) and
                 (grip is not None) and (abs(grip[0]) <= 2) and (grip[1] >= 15),
                 ' | '.join(l.split('] ', 1)[-1] for l in other + aimed) or 'no lines')
    s.lines = lines()
    s.took = time.time() - t0
    s.met = True
    s.ended = not dev.alive()
    s.tag = ''
    dev.stop()
    (s.local / 'snap64.log').write_text('\n'.join(s.lines) + '\n', encoding='utf-8')
    dev.logcat_to(s.local / 'logcat.txt', 'all')
    dev.logcat_to(s.local / 'crash.txt', 'crash')
    s.judge_end(checks)


def stage_cleanup(checks, dev, out, a):
    """A shared phone (a test lab's): the app, its own folders and every
    folder this run made, the ROM, the save and the mod in them, are removed,
    and the device is asked whether anything is left."""
    dev.stop()
    uninstall = dev.run(['uninstall', PKG]).strip()
    dev.sh('rm -rf %s /sdcard/Snap64 /sdcard/snap64check_shot.png' % ROOT)
    listing = dev.sh('ls -d %s /sdcard/Snap64 /sdcard/Android/data/%s 2>&1' % (ROOT, PKG)).strip()
    packages = dev.sh('pm list packages %s' % PKG).strip()
    folders_gone = all('No such file' in l for l in listing.splitlines() if l.strip())
    checks.add('cleanup', folders_gone and PKG not in packages,
               'adb uninstall: %s; ls: %s; pm list packages: %s'
               % (uninstall or '-', ' | '.join(listing.splitlines()), packages or '(none)'))


def boot_emulator(a):
    emu = pathlib.Path(a.emulator)
    args = [str(emu), '-avd', a.avd, '-no-snapshot', '-no-boot-anim', '-gpu', 'host', '-no-metrics']
    if not a.window:
        args.insert(3, '-no-window')
    log = open(pathlib.Path(a.out) / ('emulator-%s.log' % a.avd), 'wb')
    p = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)
    dev = Device(a.adb, a.serial)
    t0 = time.time()
    while time.time() - t0 < 300:
        if p.poll() is not None:
            raise RuntimeError('the emulator exited with %s' % p.returncode)
        if dev.sh('getprop sys.boot_completed', timeout=30).strip() == '1':
            print('the emulator %s booted in %.0f s' % (a.avd, time.time() - t0), flush=True)
            return p
        time.sleep(3)
    raise RuntimeError('the emulator did not boot in 300 s')


def main():
    global REPLAYS
    ap = argparse.ArgumentParser(description='The Android app, driven through adb.')
    ap.add_argument('--rom', required=True, help='pokemonsnap.z64')
    ap.add_argument('--save', help='the save the tapes were recorded on (saves/pokemonsnap.bin beside the PC build)')
    ap.add_argument('--mod', help='snap64_zz_hooktest.nrm (Snap64RecompMods)')
    ap.add_argument('--out', required=True, help='where each stage\'s folder goes')
    ap.add_argument('--apk', help='install this APK first and grant it all-files access')
    ap.add_argument('--stages', default=ALL_STAGES)
    ap.add_argument('--touch-settings', help='a settings file for the touch stage, as JSON (default: none)')
    ap.add_argument('--uipad', help='the pad stage\'s uinput helper (tools/android/uipad.c), built for the device')
    sdk = pathlib.Path(os.environ.get('ANDROID_HOME', pathlib.Path.home() / 'android-sdk'))
    exe = '.exe' if os.name == 'nt' else ''
    ap.add_argument('--adb', default=str(sdk / 'platform-tools' / ('adb' + exe)))
    ap.add_argument('--emulator', default=str(sdk / 'emulator' / ('emulator' + exe)))
    ap.add_argument('--avd', help='boot this virtual device first, and shut it down at the end')
    ap.add_argument('--window', action='store_true', help='show the emulator\'s window')
    ap.add_argument('--serial', help='the device, when more than one is attached')
    ap.add_argument('--cleanup', action='store_true',
                    help='at the end, uninstall the app and remove every folder the run made (a shared phone); '
                         '--stages cleanup does only that')
    ap.add_argument('--replays', default=str(REPLAYS), help='the tapes and eval.scores (tools/replays)')
    a = ap.parse_args()
    REPLAYS = pathlib.Path(a.replays)
    out = pathlib.Path(a.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    stages = [x for x in a.stages.split(',') if x]
    if stages == ['cleanup']:
        a.apk = None
    tapes = {'score', 'attract', 'stats', 'menu', 'pages', 'touch', 'touchswitch', 'touchwide', 'gyro', 'pad'}
    if (set(stages) & tapes) and not a.save:
        print('the tapes need --save, the save they were recorded on', file=sys.stderr)
        return 2
    if 'mod' in stages and not a.mod:
        print('the mod stage needs --mod', file=sys.stderr)
        return 2
    started = time.time()
    emulator = boot_emulator(a) if a.avd else None
    dev = Device(a.adb, a.serial)
    (out / 'summary.txt').write_text('', encoding='utf-8')
    checks = Checks(out / 'summary.txt')
    try:
        if not dev.online():
            checks.add('device', False, 'no device: adb get-state says %r' % dev.run(['get-state']).strip())
            return 1
        prop = lambda name: dev.sh('getprop %s' % name).strip()
        abi = prop('ro.product.cpu.abi')
        print('device: %s %s (%s), Android %s (API %s), %s, page size %s; hardware %s, chip %s %s' % (
            prop('ro.product.manufacturer'), prop('ro.product.model'), prop('ro.product.device'),
            prop('ro.build.version.release'), prop('ro.build.version.sdk'), abi, dev.sh('getconf PAGE_SIZE').strip(),
            prop('ro.hardware'), prop('ro.soc.manufacturer'), prop('ro.soc.model')), flush=True)
        # The app carries arm64-v8a and x86_64 (app/build.gradle).
        if abi not in ('arm64-v8a', 'x86_64'):
            checks.add('device', False, 'the device\'s ABI is %r; the app carries arm64-v8a and x86_64' % abi)
            stages = []
        dev.run(['logcat', '-G', '16M'])
        if a.apk and stages:
            said = dev.run(['install', '-r', a.apk], timeout=600).strip().splitlines()[-1]
            if 'INSTALL_FAILED_UPDATE_INCOMPATIBLE' in said:
                # Signed with another key than the app installed (a release
                # APK over a debug one): Android updates only over the same
                # key, so the old app goes first.
                print('install: %s; the installed app is removed first' % said, flush=True)
                dev.run(['uninstall', PKG], timeout=120)
                said = dev.run(['install', a.apk], timeout=600).strip().splitlines()[-1]
            print('install: %s' % said, flush=True)
            if 'Success' not in said:
                checks.add('install', False, said)
                stages = []
            dev.sh('appops set %s MANAGE_EXTERNAL_STORAGE allow' % PKG)
            dev.sh('appops set --uid %s MANAGE_EXTERNAL_STORAGE allow' % PKG)
        fns = {'boot': stage_boot, 'score': stage_score, 'attract': stage_attract, 'stats': stage_stats,
               'menu': stage_menu, 'pages': stage_pages, 'settings': stage_settings, 'rompick': stage_rompick,
               'mod': stage_mod, 'lifecycle': stage_lifecycle, 'touch': stage_touch,
               'touchswitch': lambda c, d, o, a: stage_touch(c, d, o, a, switch=True),
               'touchwide': lambda c, d, o, a: stage_touch(c, d, o, a, wide=True),
               'gyro': lambda c, d, o, a: stage_touch(c, d, o, a, gyro=True), 'pad': stage_pad,
               'vkcheck': stage_vkcheck,
               'cleanup': stage_cleanup}
        if a.cleanup and 'cleanup' not in stages:
            stages = stages + ['cleanup']
        for name in stages:
            if name not in fns:
                checks.add(name, False, 'no such stage')
                continue
            t = time.time()
            print('--- %s' % name, flush=True)
            try:
                fns[name](checks, dev, out, a)
            except (RuntimeError, subprocess.TimeoutExpired, OSError) as e:
                checks.add(name, False, 'the stage could not run: %s' % e)
            print('    (%s took %.0f s)' % (name, time.time() - t), flush=True)
        dev.stop()
        dev.sh('rm -rf %s' % ROOT)
    finally:
        if emulator is not None:
            dev.run(['emu', 'kill'])
            try:
                emulator.wait(timeout=60)
            except subprocess.TimeoutExpired:
                emulator.kill()
    failed = checks.failed()
    lines = ['%s  %-10s %s' % ('PASS' if ok else 'FAIL', st, tx) for st, ok, tx in checks.rows]
    (out / 'summary.txt').write_text('\n'.join(lines) + '\n\n%d checks, %d failed, %.0f s\n'
                                     % (len(checks.rows), len(failed), time.time() - started), encoding='utf-8')
    print('\n%d checks, %d failed, %.0f s' % (len(checks.rows), len(failed), time.time() - started))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
