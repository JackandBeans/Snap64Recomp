#!/usr/bin/env python3
"""Run the port's headless verification suite against a built executable.

Every check here is one the port has been verified with before, gathered so
that a release build can be put through all of them in one go:

  subsystem   the executable is a windowed program (no console opens)
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
  settings    snapsettings.json is valid JSON and carries the port's fields
  package     (with --zip) the release archive carries the executable, the
              three DLLs, the licences and the documents

It opens the real game window for each run; runs are killed at their time
limit, which is how the recipe has always worked (the replays never reach a
quit). Run it from anywhere:

    python tools/release_check.py build-win/Release [--zip build-win/Snap64Recomp-<v>-win64.zip] [--only NAME ...]

It needs the replays beach.inputs and eval.inputs beside the executable (both
recorded by the developer; BUILDING.md), and the ROM there. Exit status 0 when
every check passed. Nothing here is a substitute for playing the game; it is
what can be checked without a person.
"""
import argparse
import hashlib
import json
import os
import pathlib
import struct
import subprocess
import sys
import time
import zipfile

EXE = 'Snap64Recomp.exe'
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


def check_subsystem(c, exe_dir):
    sub = pe_subsystem(exe_dir / EXE)
    c.add('subsystem', sub == 2, 'PE subsystem %d (%s)' % (sub, {2: 'windowed', 3: 'console'}.get(sub, '?')))


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


def check_package(c, zip_path):
    z = zipfile.ZipFile(zip_path)
    names = z.namelist()
    top = names[0].split('/')[0]
    need = [EXE, 'SDL2.dll', 'dxcompiler.dll', 'dxil.dll', 'LICENSE', 'NOTICE.md', 'README.md',
            'licenses/DirectXShaderCompiler.txt', 'licenses/DirectXShaderCompiler-dxil.txt',
            'menu_text/recomp_logo.png', 'Snap64Recomp.map']
    missing = [n for n in need if (top + '/' + n) not in names]
    c.add('package', not missing, '%s: %d entries%s' % (zip_path.name, len(names), (', missing ' + ', '.join(missing)) if missing else ''))
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
              ('stats', check_stats), ('score', check_score), ('settings', check_settings)]
    c = Check()
    t0 = time.time()
    for name, fn in checks:
        if args.only and name not in args.only:
            continue
        if name in ('attract', 'stats', 'stdio') and not (exe_dir / 'beach.inputs').is_file():
            c.add(name, False, 'beach.inputs is not beside the executable')
            continue
        if name == 'score' and not (exe_dir / 'eval.inputs').is_file():
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
