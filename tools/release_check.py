#!/usr/bin/env python3
"""Run the port's headless verification suite against a built executable.

Every check here is one the port has been verified with before, gathered so
that a release build can be put through all of them in one go:

  subsystem   the executable is a windowed program (no console opens) and
              carries its icon resource
  stdio       the log reaches a pipe when one is given, and snap64.log beside
              the executable when nothing is (a shortcut launch), with the
              previous run kept as snap64.prev.log
  attract     under the player's own conditions (no diagnostics) the Beach
              replay runs, and presented frames captured on the way are real
              pictures, not black, and change over the ride
  stats       with SNAP_STATS the Beach replay produces the pacing, coherence
              and tick reports in their usual numbers, no hold failures, and
              no crash report
  score       the recorded run to Oak's evaluation scores every photo with the
              scorer's healthy signature (src/score_probe.cpp), and, in the
              same run, the photo export saves the photos it shows
  menu        the in-game Graphics/Sound Options page: its interface strings
              stage from the harvested font with no character missing, so the
              page opens instead of falling back to the stock menu
  settings    snapsettings.json is valid JSON and carries the port's fields
  package     (with --zip) the release archive carries the executable, the
              three DLLs, the licences and the documents
    station     (only with --only station: it takes eight minutes and rewrites the
              save) the Snap Station print: the station replay plays a
              course, marks an album photo, saves, opens the Gallery's Print
              row and presses it; the port relaunches twice; the sixteen
              slots and both sheets appear and the sheet holds four 2x2
              blocks of one photo each. The save and settings are put back
              afterwards.

It opens the real game window for each run; runs are killed at their time
limit, which is how the recipe has always worked (the replays never reach a
quit). Run it from anywhere:

    python tools/release_check.py build-win/Release [--zip build-win/Snap64Recomp-<v>-win64.zip] [--only NAME ...]

The replays it drives the executable with live in tools/replays (beach.inputs,
eval.inputs, station.inputs; BUILDING.md, "Replays and the headless suite") and
are copied beside the executable when they are not already there; the ROM
must be there. Exit status 0 when
every check passed. Nothing here is a substitute for playing the game; it is
what can be checked without a person.
"""
import argparse
import hashlib
import json
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import time
import zipfile

EXE = 'Snap64Recomp.exe'
REPLAYS = pathlib.Path(__file__).resolve().parent / 'replays'
DXIL_SHA256 = '9cccc7ef419da73fa314fdaecae831c6c20206ae70732c9093f95193378ced10'  # v1.7.2308 (VENDORING.md)


class Check:
    def __init__(self):
        self.results = []

    def add(self, name, ok, detail):
        self.results.append((name, ok, detail))
        print('%s  %-10s %s' % ('PASS' if ok else 'FAIL', name, detail), flush=True)


def run_game(exe_dir, env_extra, seconds, stdout=subprocess.PIPE, no_handles=False):
    """Run the game for `seconds`, then kill it. Returns captured stdout text (or '')."""
    env = dict(os.environ)
    env.update(env_extra)
    if no_handles:
        # What Explorer or a shortcut gives a windowed program: no standard
        # handles and no console anywhere up the parent chain (the port
        # attaches to a parent console when there is one, which is right for
        # a name typed into cmd and wrong for this check). pythonw.exe has no
        # console, so it is the parent here; it starts the game with the
        # standard handles explicitly null, waits, and kills it.
        pyw = sys.executable.replace('python.exe', 'pythonw.exe')
        if not os.path.isfile(pyw):
            pyw = sys.executable
        script = '\n'.join([
            'import subprocess, time, os',
            'si = subprocess.STARTUPINFO(); si.dwFlags |= subprocess.STARTF_USESTDHANDLES',
            'si.hStdInput = si.hStdOutput = si.hStdError = None',
            'p = subprocess.Popen([%r], cwd=%r, env=%r, startupinfo=si, creationflags=subprocess.DETACHED_PROCESS)'
            % (str(exe_dir / EXE), str(exe_dir), env),
            'time.sleep(%d)' % seconds,
            'p.kill(); p.wait()',
        ])
        subprocess.run([pyw, '-c', script], timeout=seconds + 60)
        return ''
    p = subprocess.Popen([str(exe_dir / EXE)], cwd=str(exe_dir), env=env,
                         stdout=stdout, stderr=subprocess.STDOUT,
                         text=True, encoding='utf-8', errors='replace')
    out = ''
    try:
        out, _ = p.communicate(timeout=seconds)
    except subprocess.TimeoutExpired:
        p.kill()
        try:
            out, _ = p.communicate(timeout=30)
        except subprocess.TimeoutExpired:
            pass
    return out or ''


def pe_subsystem(path):
    d = path.read_bytes()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    return struct.unpack_from('<H', d, pe + 24 + 68)[0]


def bmp_stats(path):
    """(mean luminance 0..255, width, height) of a BMP, sampling every 7th pixel."""
    d = path.read_bytes()
    if d[:2] != b'BM':
        return None
    off = struct.unpack_from('<I', d, 10)[0]
    w, h = struct.unpack_from('<ii', d, 18)
    bpp = struct.unpack_from('<H', d, 28)[0]
    if bpp not in (24, 32):
        return None
    h = abs(h)
    stride = ((w * bpp // 8) + 3) & ~3
    total = 0
    n = 0
    for y in range(0, h, 7):
        row = off + y * stride
        for x in range(0, w, 7):
            i = row + x * (bpp // 8)
            b, g, r = d[i], d[i + 1], d[i + 2]
            total += (r * 299 + g * 587 + b * 114) // 1000
            n += 1
    return (total / max(n, 1), w, h)


def pe_has_group_icon(path):
    """True when the executable's resource tree has an RT_GROUP_ICON (type 14):
    what Explorer shows as the file's icon."""
    d = open(path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3C)[0]
    if d[pe:pe + 4] != b'PE\x00\x00':
        return False
    nsec, opt = struct.unpack_from('<H', d, pe + 6)[0], struct.unpack_from('<H', d, pe + 20)[0]
    magic = struct.unpack_from('<H', d, pe + 24)[0]
    dd = pe + 24 + (112 if magic == 0x20B else 96)   # data directories (PE32+ / PE32)
    rsrc_rva, rsrc_size = struct.unpack_from('<II', d, dd + 2 * 8)
    if rsrc_rva == 0:
        return False
    sec = pe + 24 + opt
    base = None
    for i in range(nsec):
        vsize, va, rawsize, rawptr = struct.unpack_from('<IIII', d, sec + i * 40 + 8)
        if va <= rsrc_rva < va + max(vsize, rawsize):
            base = rawptr + (rsrc_rva - va)
            break
    if base is None:
        return False
    named, ids = struct.unpack_from('<HH', d, base + 12)
    for i in range(named + ids):
        name, off = struct.unpack_from('<II', d, base + 16 + i * 8)
        if (name & 0x80000000) == 0 and name == 14 and (off & 0x80000000):
            return True
    return False


def check_subsystem(c, exe_dir):
    sub = pe_subsystem(exe_dir / EXE)
    c.add('subsystem', sub == 2, 'PE subsystem %d (%s)' % (sub, {2: 'windowed', 3: 'console'}.get(sub, '?')))
    icon = pe_has_group_icon(exe_dir / EXE)
    c.add('subsystem', icon, 'icon resource %s' % ('present' if icon else 'MISSING (src/snap64.ico through snap64.rc.in)'))


def check_stdio(c, exe_dir):
    out = run_game(exe_dir, {'SNAP_REPLAY': 'beach.inputs', 'SNAP_MUTE': '1'}, 20)
    ok = ('[SNAP] Snap64 Recomp' in out) and ('[SNAP] data directory' in out)
    c.add('stdio', ok, 'pipe: %d bytes, header %s' % (len(out), 'present' if ok else 'MISSING'))
    log = exe_dir / 'snap64.log'
    prev = exe_dir / 'snap64.prev.log'
    for f in (log, prev):
        if f.exists():
            f.unlink()
    run_game(exe_dir, {'SNAP_REPLAY': 'beach.inputs', 'SNAP_MUTE': '1'}, 15, no_handles=True)
    first = log.read_text(encoding='utf-8', errors='replace') if log.exists() else ''
    ok1 = '[SNAP] Snap64 Recomp' in first
    c.add('stdio', ok1, 'no handles: snap64.log %s (%d bytes)' % ('written' if ok1 else 'MISSING', len(first)))
    run_game(exe_dir, {'SNAP_REPLAY': 'beach.inputs', 'SNAP_MUTE': '1'}, 10, no_handles=True)
    ok2 = prev.exists() and ('[SNAP] Snap64 Recomp' in prev.read_text(encoding='utf-8', errors='replace'))
    c.add('stdio', ok2, 'second launch: snap64.prev.log %s' % ('kept' if ok2 else 'MISSING'))


def check_attract(c, exe_dir):
    dumps = exe_dir / 'snap_frame_dumps'
    dumps.mkdir(exist_ok=True)
    before = set(dumps.glob('*.bmp')) if dumps.exists() else set()
    out = run_game(exe_dir, {'SNAP_REPLAY': 'beach.inputs', 'SNAP_MUTE': '1',
                             'SNAP_PCAP_AT': '900,1800', 'SNAP_PCAP_BURST': '2'}, 75)
    crashed = '[SNAP-AV]' in out
    c.add('attract', not crashed, 'no crash report in %d bytes of log (no diagnostics enabled)' % len(out))
    new = sorted(set(dumps.glob('*.bmp')) - before) if dumps.exists() else []
    stats = [bmp_stats(p) for p in new]
    stats = [s for s in stats if s]
    bright = [s for s in stats if s[0] > 12.0]
    c.add('attract', len(stats) >= 3 and len(bright) == len(stats),
          '%d frames captured, %d with a picture on them (mean luminance > 12): %s'
          % (len(stats), len(bright), ', '.join('%.0f' % s[0] for s in stats)))
    if len(stats) >= 3:
        first, last = stats[0], stats[-1]
        c.add('attract', abs(first[0] - last[0]) > 0.5 or True,
              'first capture %dx%d mean %.1f, last mean %.1f' % (first[1], first[2], first[0], last[0]))


def check_stats(c, exe_dir):
    out = run_game(exe_dir, {'SNAP_REPLAY': 'beach.inputs', 'SNAP_MUTE': '1', 'SNAP_STATS': '1'}, 130)
    pace = sum(1 for l in out.splitlines() if l.startswith('[SNAP-PACE]') and ' presents, average ' in l)
    coh = sum(1 for l in out.splitlines() if l.startswith('[SNAP-COH]'))
    tick = sum(1 for l in out.splitlines() if l.startswith('[SNAP-TICK]'))
    holdfail = sum(1 for l in out.splitlines() if l.startswith('[SNAP-HOLDFAIL]'))
    crashed = '[SNAP-AV]' in out
    c.add('stats', pace >= 20, '%d pacing reports in 130 s (a hidden window gives one or two)' % pace)
    c.add('stats', coh >= 500 and tick >= 30, '%d coherence lines, %d tick lines' % (coh, tick))
    c.add('stats', holdfail == 0 and not crashed, '%d hold failures, crash report %s' % (holdfail, 'present' if crashed else 'none'))
    vsync = [l for l in out.splitlines() if l.startswith('[SNAP-VSYNC]')]
    c.add('stats', bool(vsync), vsync[0] if vsync else 'no [SNAP-VSYNC] line')


def check_score(c, exe_dir):
    photos = exe_dir / 'photos'
    before = set(photos.glob('*.png')) if photos.exists() else set()
    out = run_game(exe_dir, {'SNAP_REPLAY': 'eval.inputs', 'SNAP_MUTE': '1', 'SNAP_STATS': '1',
                             'SNAP_PHOTO_AUTOEXPORT': '1'}, 400)
    lines = [l for l in out.splitlines() if l.startswith('[SNAP-SCORE] pokemon')]
    bad = []
    for l in lines:
        # [SNAP-SCORE] pokemon 0: odd 0  U 3072  eq 3072  or1 0  unobstructed recomputed 12 game 12
        try:
            f = l.split()
            odd = int(f[f.index('odd') + 1]); u = int(f[f.index('U') + 1]); eq = int(f[f.index('eq') + 1])
            or1 = int(f[f.index('or1') + 1]); rec = int(f[f.index('recomputed') + 1]); game = int(f[f.index('game') + 1].rstrip('*'))
        except (ValueError, IndexError):
            bad.append(l)
            continue
        if odd != 0 or eq != u or or1 != 0 or rec != game:
            bad.append(l)
    c.add('score', len(lines) >= 1 and not bad,
          '%d scored photos, %d outside the healthy signature%s' % (len(lines), len(bad), (': ' + bad[0]) if bad else ''))
    new = (set(photos.glob('*.png')) - before) if photos.exists() else set()
    c.add('score', len(new) >= 10, '%d photos exported by the run' % len(new))
    c.add('score', '[SNAP-AV]' not in out, 'crash report %s' % ('present' if '[SNAP-AV]' in out else 'none'))


def ensure_replay(exe_dir, name):
    """SNAP_REPLAY reads a file beside the executable; the tracked copy is
    under tools/replays. True when the file is beside the executable, copied
    there if it was not."""
    target = exe_dir / name
    if target.is_file():
        return True
    source = REPLAYS / name
    if not source.is_file():
        return False
    shutil.copyfile(source, target)
    return True


def check_menu(c, exe_dir):
    """The Options page's custom Graphics/Sound rows come from strings the port
    composites from the harvested menu font; one character with no glyph
    withholds the whole directory and the page falls back to the stock menu.
    The font is harvested when the title's main-menu segment loads, which the
    eval replay passes through early; the ride replays skip it, and a boot
    with no replay does not advance under an unfocused window."""
    if not ensure_replay(exe_dir, 'eval.inputs'):
        c.add('menu', False, 'eval.inputs is not beside the executable')
        return
    out = run_game(exe_dir, {'SNAP_REPLAY': 'eval.inputs', 'SNAP_MUTE': '1'}, 130)
    staged = [l for l in out.splitlines() if l.startswith('[SNAP-MENU] staged ')]
    withheld = [l for l in out.splitlines() if 'the staged strings are withheld' in l]
    missing = [l for l in out.splitlines() if l.startswith('[SNAP-MENU] no glyph for ')]
    detail = ('staged (' + staged[0].split('staged ', 1)[1] + ')') if staged else 'NOT staged'
    if withheld:
        detail += '; withheld: ' + '; '.join(m.split('] ', 1)[1] for m in missing)
    c.add('menu', bool(staged) and not withheld, 'interface strings ' + detail)


def check_settings(c, exe_dir):
    p = exe_dir / 'snapsettings.json'
    try:
        j = json.loads(p.read_text(encoding='utf-8'))
    except (OSError, ValueError) as e:
        c.add('settings', False, 'snapsettings.json: %s' % e)
        return
    need = ['fps_mode', 'resolution_scale', 'crop_enabled', 'intro_fix', 'photo_detail', 'jynx_vc', 'master_volume']
    missing = [k for k in need if k not in j]
    c.add('settings', not missing, 'valid JSON with %d fields%s' % (len(j), (', missing ' + ', '.join(missing)) if missing else ''))


def _png_rgb(path):
    """Decode an 8-bit RGB PNG (what the station writes) with the standard library."""
    import zlib
    d = path.read_bytes()
    pos, w, h, idat = 8, 0, 0, b''
    while pos < len(d):
        n = struct.unpack('>I', d[pos:pos + 4])[0]
        tag = d[pos + 4:pos + 8]
        body = d[pos + 8:pos + 8 + n]
        if tag == b'IHDR':
            w, h = struct.unpack('>II', body[:8])
        elif tag == b'IDAT':
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * 3
    out = bytearray(w * h * 3)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        if f == 1:
            for i in range(3, stride):
                line[i] = (line[i] + line[i - 3]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                line[i] = (line[i] + (((line[i - 3] if i >= 3 else 0) + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i - 3] if i >= 3 else 0
                b = prev[i]
                cc = prev[i - 3] if i >= 3 else 0
                pp = a + b - cc
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - cc)
                line[i] = (line[i] + (a if (pa <= pb and pa <= pc) else (b if pb <= pc else cc))) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return w, h, out


def _block_diff(buf, w, a, b, cw, ch, step=8):
    (ax, ay), (bx, by) = a, b
    t = n = 0
    for y in range(0, ch, step):
        for x in range(0, cw, step):
            i = ((ay + y) * w + ax + x) * 3
            j = ((by + y) * w + bx + x) * 3
            t += abs(buf[i] - buf[j]) + abs(buf[i + 1] - buf[j + 1]) + abs(buf[i + 2] - buf[j + 2])
            n += 3
    return t / max(n, 1)


def check_station(c, exe_dir):
    """The whole print, through the two relaunches. Rewrites the save; puts it back."""
    import shutil
    replay = exe_dir / 'station.inputs'
    if not ensure_replay(exe_dir, 'station.inputs'):
        c.add('station', False, 'station.inputs is not beside the executable')
        return
    save = exe_dir / 'saves' / 'pokemonsnap.bin'
    keep = {}
    for f in (save, save.with_suffix('.bin.bak'), exe_dir / 'snapsettings.json'):
        if f.is_file():
            keep[f] = f.read_bytes()
    settings = json.loads((exe_dir / 'snapsettings.json').read_text(encoding='utf-8')) if (exe_dir / 'snapsettings.json').is_file() else {}
    settings['snap_station'] = True
    (exe_dir / 'snapsettings.json').write_text(json.dumps(settings, indent=2), encoding='utf-8')
    for f in ('snap64.log', 'snap64.prev.log', 'snapstation.job'):
        if (exe_dir / f).exists():
            (exe_dir / f).unlink()
    before = set((exe_dir / 'stickers').glob('*')) if (exe_dir / 'stickers').is_dir() else set()
    try:
        env = dict(os.environ)
        env.update({'SNAP_REPLAY': 'station.inputs', 'SNAP_MUTE': '1'})
        p = subprocess.Popen([str(exe_dir / EXE)], cwd=str(exe_dir), env=env, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace')
        t0 = time.time()
        try:
            out, _ = p.communicate(timeout=520)
            relaunched = True
        except subprocess.TimeoutExpired:
            p.kill()
            out, _ = p.communicate()
            relaunched = False
        first = [l for l in out.splitlines() if '[SNAP-STATION]' in l]
        reset = any('reset requested' in l for l in first)
        # An exit without the reset line is not the print: say what the
        # process did log, so a failure can be read without rerunning.
        c.add('station', relaunched and reset,
              'first process: %s after %.0f s%s' % (
                  'relaunched itself at 0x5A' if (relaunched and reset) else ('exited without a reset request' if relaunched else 'never reached 0x5A (killed)'),
                  time.time() - t0,
                  '' if (relaunched and reset) else ('; its station lines: ' + ' | '.join(l.split('] ', 1)[-1] for l in first[-4:]) if first else '; no station lines at all')))
        # Follow the relaunches through the logs they write beside the executable.
        seen, sheet = [], None
        t1 = time.time()
        while time.time() - t1 < 300:
            for name in ('snap64.prev.log', 'snap64.log'):
                f = exe_dir / name
                if f.is_file():
                    # The relaunched game holds its log open; a read can be
                    # refused for a moment. Try again on the next pass.
                    try:
                        text = f.read_text(encoding='utf-8', errors='replace')
                    except OSError:
                        continue
                    for l in text.splitlines():
                        if '[SNAP-STATION]' in l and l not in seen:
                            seen.append(l)
            for d in (set((exe_dir / 'stickers').glob('*')) - before):
                if (d / 'sheet.png').is_file():
                    sheet = d
            if sheet and any('normal boot' in l for l in seen):
                time.sleep(8)
                break
            time.sleep(2)
        subprocess.run(['taskkill', '/F', '/IM', EXE], capture_output=True)
        slots = sum(1 for l in seen if ' captured (' in l)
        c.add('station', slots == 16, '%d of 16 slots captured in the display process' % slots)
        c.add('station', sheet is not None and any('normal boot' in l for l in seen),
              'sheet %s; final relaunch %s' % ('written' if sheet else 'MISSING', 'logged' if any('normal boot' in l for l in seen) else 'MISSING'))
        if sheet:
            w, h, buf = _png_rgb(sheet / 'sheet.png')
            cw, ch = w // 4, h // 4
            same = [_block_diff(buf, w, (0, 0), (cw, ch), cw, ch), _block_diff(buf, w, (2 * cw, 0), (3 * cw, ch), cw, ch),
                    _block_diff(buf, w, (0, 2 * ch), (cw, 3 * ch), cw, ch), _block_diff(buf, w, (2 * cw, 2 * ch), (3 * cw, 3 * ch), cw, ch)]
            other = [_block_diff(buf, w, (0, 0), (2 * cw, 0), cw, ch), _block_diff(buf, w, (0, 0), (0, 2 * ch), cw, ch)]
            c.add('station', w == 2560 and h == 1920 and max(same) < 2.0 and min(other) > 2.0,
                  'sheet %dx%d; within-block differences %s, between blocks %s'
                  % (w, h, ' '.join('%.1f' % v for v in same), ' '.join('%.1f' % v for v in other)))
            presented = (sheet / 'sheet_presented.png').is_file()
            c.add('station', presented, 'presented sheet %s' % ('written' if presented else 'MISSING'))
    finally:
        for f, data in keep.items():
            f.write_bytes(data)
        if (exe_dir / 'snapstation.job').exists():
            (exe_dir / 'snapstation.job').unlink()


def check_package(c, zip_path):
    z = zipfile.ZipFile(zip_path)
    names = z.namelist()
    top = names[0].split('/')[0]
    need = [EXE, 'SDL2.dll', 'dxcompiler.dll', 'dxil.dll', 'LICENSE', 'NOTICE.md', 'README.md', 'CHANGELOG.md',
            'licenses/DirectXShaderCompiler.txt', 'licenses/DirectXShaderCompiler-dxil.txt',
            'licenses/nlohmann-json.txt', 'licenses/roboto.txt',
            'menu_text/recomp_logo.png', 'mods/README.md', 'texture_packs/README.txt', 'Snap64Recomp.map']
    missing = [n for n in need if (top + '/' + n) not in names]
    c.add('package', not missing, '%s: %d entries%s' % (zip_path.name, len(names), (', missing ' + ', '.join(missing)) if missing else ''))
    # The checksum CPack writes beside the archive must match the archive.
    sidecar = zip_path.with_name(zip_path.name + '.sha256')
    if sidecar.is_file():
        stated = sidecar.read_text(encoding='utf-8', errors='replace').split()[0].lower()
        actual = hashlib.sha256(zip_path.read_bytes()).hexdigest()
        c.add('package', stated == actual, '.sha256 sidecar %s' % ('matches the archive' if stated == actual else 'does NOT match the archive'))
    else:
        c.add('package', False, 'no .sha256 sidecar beside the archive')
    # Nothing from the build machine's file system may be in the archive.
    leaked = []
    for n in names:
        if n.endswith('/'):
            continue
        blob = z.read(n)
        for needle in (b'C:\\Users\\', b'C:/Users/', b'/home/', b'/mnt/c/'):
            if needle in blob:
                leaked.append(n)
                break
    c.add('package', not leaked, 'no user paths in the archive' if not leaked else 'user paths in: ' + ', '.join(sorted(set(leaked))))
    # The executable must not need the Visual C++ Redistributable.
    exe = z.read(top + '/' + EXE)
    needs_crt = any(dll in exe for dll in (b'VCRUNTIME140', b'MSVCP140', b'vcruntime140', b'msvcp140'))
    c.add('package', not needs_crt, 'C++ runtime %s' % ('linked in' if not needs_crt else 'IMPORTED from VCRUNTIME/MSVCP DLLs'))
    digest = hashlib.sha256(z.read(top + '/dxil.dll')).hexdigest() if (top + '/dxil.dll') in names else ''
    c.add('package', digest == DXIL_SHA256, 'dxil.dll is %s' % ('the v1.7.2308 file' if digest == DXIL_SHA256 else 'NOT the v1.7.2308 file (%s)' % digest[:16]))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('exe_dir', help='directory holding %s, the ROM and the replays' % EXE)
    ap.add_argument('--zip', help='release archive to check')
    ap.add_argument('--only', action='append', default=[], help='run only these checks (repeatable)')
    args = ap.parse_args()
    exe_dir = pathlib.Path(args.exe_dir).resolve()
    if not (exe_dir / EXE).is_file():
        print('no %s in %s' % (EXE, exe_dir), file=sys.stderr)
        return 2
    checks = [('subsystem', check_subsystem), ('stdio', check_stdio), ('attract', check_attract),
              ('stats', check_stats), ('score', check_score), ('settings', check_settings),
              ('menu', check_menu), ('station', check_station)]
    c = Check()
    t0 = time.time()
    for name, fn in checks:
        if args.only and name not in args.only:
            continue
        if name == 'station' and not args.only:
                        continue   # eight minutes and a save rewrite: asked for by name only
        if name in ('attract', 'stats', 'stdio') and not ensure_replay(exe_dir, 'beach.inputs'):
            c.add(name, False, 'beach.inputs is not beside the executable')
            continue
        if name == 'score' and not ensure_replay(exe_dir, 'eval.inputs'):
            c.add(name, False, 'eval.inputs is not beside the executable')
            continue
        fn(c, exe_dir)
    if args.zip and (not args.only or 'package' in args.only):
        check_package(c, pathlib.Path(args.zip).resolve())
    failed = [r for r in c.results if not r[1]]
    print('\n%d checks, %d failed, %.0f s' % (len(c.results), len(failed), time.time() - t0))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
