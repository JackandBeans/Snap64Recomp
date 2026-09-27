#!/usr/bin/env python3
"""The macOS bundle on a Mac with no one at it: GitHub's runner.

    python3 tools/macos_smoke.py build-macos/Snap64Recomp.app --inputs <folder> --out <folder> [--arch arm64|x86_64]

The runner is a virtual Mac on Apple Silicon whose GPU is Apple's
paravirtual one: Metal works there for correctness, not for speed. This
script runs the bundle's executable the way a player's first start does --
a fresh data folder (SNAP_DATA_DIR) holding only what that stage needs -- and
reads what it printed. `--arch x86_64` runs the Intel half of a universal
build through Rosetta 2, which is how an Intel Mac's code path gets run here
at all. Each stage has its own folder under --out with the log, and the
folder with its captured frames; a summary goes to stdout and, on GitHub, to
the job's summary page. The exit code is 1 when a check failed.

The stages:

* boot: the title screen from a cold start. The version and "graphics
  API: Metal" lines, three presented frames (SNAP_PCAP_ATFRAME) with a
  picture on them, no crash.
* validated: the same boot with Metal's API validation on (MTL_DEBUG_LAYER),
  which reports a misuse of the API on stderr that a real GPU might punish
  where the paravirtual one does not. Its "failed assertion" lines fail it.
* score: the release suite's scoring replay (tools/replays/eval.inputs, on
  the save it was recorded with). The ride's fifteen photos must score --
  the first 37 of the reference's 45 lines, the same subjects per photo as
  the Windows build's (tools/replays/eval.scores), every line inside the
  healthy signature -- and the numbers are compared line by line and
  reported, not failed: the renderer's depth readback feeds them, and a GPU
  is not bound to agree with another to the unit (the runner's paravirtual
  one differs by a few units on most photos). The last eight lines are
  Oak's evaluation in the lab, and a tape of presses per pad reading falls
  out of step there on any machine that runs slow (a PC with every tick
  made 25 ms longer stops at the same 37); whether the runner reaches them
  is reported.
* mod: a mod that hooks three game functions (Snap64RecompMods'
  zz_hooktest) loads and its hooks run: the proof that the code segment's
  maximum protection (tools/macos/ld64) and the entitlements
  (tools/macos/entitlements.plist) let librecomp patch the executable and
  run the mod's recompiled code.

Crash reports macOS wrote for the executable during the run
(~/Library/Logs/DiagnosticReports) are copied into --out.

--inputs holds what no public tree may: pokemonsnap.z64, saves/pokemonsnap.bin
(the save eval.inputs was recorded on) and mods/snap64_zz_hooktest.nrm.
"""
import argparse
import os
import pathlib
import re
import shutil
import signal
import struct
import subprocess
import sys
import threading
import time

TOOLS = pathlib.Path(__file__).resolve().parent
REPLAYS = TOOLS / 'replays'
DIAG = pathlib.Path.home() / 'Library' / 'Logs' / 'DiagnosticReports'


class Checks:
    def __init__(self):
        self.rows = []

    def add(self, stage, ok, text):
        self.rows.append((stage, bool(ok), text))
        print('%s  %-10s %s' % ('PASS' if ok else 'FAIL', stage, text), flush=True)

    def failed(self):
        return [r for r in self.rows if not r[1]]


def run_game(binary, arch, workdir, env_extra, cap, done=None, after=0.0):
    """Runs the executable in workdir (its data folder) until done(lines)
    is true, then `after` seconds more, or `cap` seconds in all. Returns
    (lines, exit: an int when the program ended on its own, None when it was
    stopped here, seconds)."""
    env = dict(os.environ)
    env.update({'SNAP_DATA_DIR': str(workdir), 'SNAP_MUTE': '1', 'SNAP_WINDOW': '640x480',
                'SNAP_ROM_PICK': 'cancel'})
    env.update(env_extra)
    cmd = [str(binary)]
    if arch == 'x86_64':
        cmd = ['arch', '-x86_64'] + cmd
    p = subprocess.Popen(cmd, cwd=str(workdir), env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True, encoding='utf-8', errors='replace')
    lines = []
    lock = threading.Lock()

    def reader():
        for line in p.stdout:
            with lock:
                lines.append(line.rstrip('\n'))

    th = threading.Thread(target=reader, daemon=True)
    th.start()
    t0 = time.time()
    met_at = None
    while True:
        if p.poll() is not None:
            break
        now = time.time()
        if now - t0 >= cap:
            break
        if done is not None and met_at is None:
            with lock:
                snapshot = list(lines)
            if done(snapshot):
                met_at = now
        if met_at is not None and now - met_at >= after:
            break
        time.sleep(0.5)
    exited = p.poll()
    if exited is None:
        p.send_signal(signal.SIGTERM)
        try:
            p.wait(timeout=10)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait(timeout=10)
    th.join(timeout=10)
    return lines, exited, time.time() - t0


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


def new_workdir(out, name, inputs, save=False, mods=()):
    work = out / name
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    shutil.copyfile(inputs / 'pokemonsnap.z64', work / 'pokemonsnap.z64')
    if save:
        (work / 'saves').mkdir()
        shutil.copyfile(inputs / 'saves' / 'pokemonsnap.bin', work / 'saves' / 'pokemonsnap.bin')
    if mods:
        (work / 'mods').mkdir()
        for m in mods:
            shutil.copyfile(inputs / 'mods' / m, work / 'mods' / m)
    return work


def keep_log(work, lines):
    (work / 'stdout.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')


def ended(checks, stage, exited, lines, work=None):
    """A program that ended by itself before it was stopped has crashed or
    quit: either way the stage did not finish. The port writes its stderr
    into snap64.log in the data folder and echoes it from there, and an abort
    kills it before the echo, so the abort's own message (an assertion, an
    uncaught exception) is in that file and nowhere else: it is read and said."""
    if exited is None:
        checks.add(stage, True, 'ran until stopped')
        return
    tail = [l for l in lines[-6:] if l.strip()]
    why = ''
    if work is not None and (work / 'snap64.log').is_file():
        log = (work / 'snap64.log').read_text(encoding='utf-8', errors='replace').splitlines()
        marks = [l for l in log if re.search(r'Assertion failed|libc\+\+abi|terminating|Trace/BPT|Segmentation|Bus error|abort', l)]
        if marks:
            why = '; the log says: ' + ' | '.join(marks[-2:])
        elif log:
            why = '; the log ends: ' + ' | '.join(l for l in log[-3:] if l.strip())
    checks.add(stage, False, 'the program ended by itself with status %d%s; last lines: %s' % (exited, why, ' | '.join(tail)))


def stage_boot(checks, binary, arch, out, inputs, validated):
    stage = ('validated' if validated else 'boot') + '-' + arch
    work = new_workdir(out, stage, inputs)
    env = {'SNAP_PCAP_ATFRAME': '300,900,1500', 'SNAP_PCAP_BURST': '1'}
    if not validated:
        # The game's own frame-rate lines, for the pace report below; the
        # validated boot runs without the statistics, as a player's does.
        env['SNAP_STATS'] = '1'
    if validated:
        env.update({'MTL_DEBUG_LAYER': '1', 'MTL_DEBUG_LAYER_ERROR_MODE': 'nslog',
                    'MTL_DEBUG_LAYER_WARNING_MODE': 'nslog'})

    def three_frames(lines):
        return sum(1 for l in lines if l.startswith('[SNAP-PCAP] wrote')) >= 3

    lines, exited, took = run_game(binary, arch, work, env, 240, three_frames, 3.0)
    keep_log(work, lines)
    text = '\n'.join(lines)
    version = next((l for l in lines if l.startswith('[SNAP] Snap64 Recomp ')), None)
    checks.add(stage, version is not None, version or 'no version line')
    api = next((l for l in lines if 'graphics API' in l), None)
    checks.add(stage, (api is not None) and ('Metal' in api), api or 'no graphics API line')
    device = next((l for l in lines if l.startswith('Device Name:') or 'Metal device' in l), None)
    if device:
        print('           %s' % device)
    frames = sorted((work / 'snap_frame_dumps').glob('*.bmp')) if (work / 'snap_frame_dumps').exists() else []
    means = [bmp_mean(f) for f in frames]
    lit = [m for m in means if (m is not None) and (m > 12)]
    checks.add(stage, len(lit) >= 1, '%d frames captured, %d with a picture (mean luminance %s) in %.0f s'
               % (len(frames), len(lit), ', '.join('%.0f' % m for m in means if m is not None) or '-', took))
    ended(checks, stage, exited, lines, work)
    # The pace the game held, from its own reports (SNAP_STATS): the title
    # and the intro run at 60 on a PC. Reported, not judged.
    rates = [float(m.group(1)) for m in (re.search(r'average [\d.]+ ms \(([\d.]+) fps\)', l) for l in lines) if m]
    if rates:
        print('           game frame rate over %d reports: mean %.1f fps, lowest %.1f, highest %.1f'
              % (len(rates), sum(rates) / len(rates), min(rates), max(rates)), flush=True)
    if validated:
        failures = [l for l in lines if 'failed assertion' in l]
        notes = [l for l in lines if ('MTLDebug' in l or 'Metal API Validation' in l or 'validation' in l.lower())
                 and 'failed assertion' not in l]
        checks.add(stage, not failures, 'Metal API validation: %d failed assertions%s; %d other validation lines'
                   % (len(failures), (': ' + failures[0][:200]) if failures else '', len(notes)))
    return text


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


def stage_score(checks, binary, arch, out, inputs):
    stage = 'score-' + arch
    work = new_workdir(out, stage, inputs, save=True)
    shutil.copyfile(REPLAYS / 'eval.inputs', work / 'eval.inputs')
    env = {'SNAP_REPLAY': 'eval.inputs', 'SNAP_STATS': '1', 'SNAP_PHOTO_AUTOEXPORT': '1'}
    # Done at the 45th score, or ninety seconds after the album's first
    # photo render (the ride is over and the lab reached); fifteen minutes
    # at most.
    def done(ls):
        return (len(score_lines(ls)) >= 45) or any(l.startswith('[SNAP-PHOTO] render #') for l in ls)

    lines, exited, took = run_game(binary, arch, work, env, 900, done, 90.0)
    keep_log(work, lines)
    scores = score_lines(lines)
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
    ref = [l.rstrip('\n') for l in (REPLAYS / 'eval.scores').read_text(encoding='utf-8').splitlines() if l.strip()]
    ref_groups = score_groups(ref)
    got_groups = score_groups(scores)
    in_step = 0
    for a, b in zip(got_groups, ref_groups):
        if a != b:
            break
        in_step += 1
    checks.add(stage, (len(scores) >= 20) and not bad,
               "%d scored lines in %.0f s (the reference has 45), %d outside the healthy signature%s; "
               "in step with Windows for %d of its %d photos" % (len(scores), took, len(bad), (': ' + bad[0]) if bad else '',
                                                                 in_step, len(ref_groups)))
    if in_step < len(ref_groups):
        print("           the tape fell out of step after photo %d. A tape of presses per pad reading holds only while the "
              "machine keeps the console's pace at every press; a shared virtual Mac does not always, and a PC with every "
              "tick made 25 ms longer stops in the lab the same way. The lines scored are the check." % in_step, flush=True)
    photos = list((work / 'photos').glob('*.png')) if (work / 'photos').exists() else []
    checks.add(stage, len(photos) >= 10, '%d photos exported' % len(photos))
    same = sum(1 for a, b in zip(scores, ref) if a == b)
    print('           against the Windows build: %d of %d score lines identical' % (same, len(ref)), flush=True)
    for a, b in zip(scores, ref):
        if a != b:
            print('             mac:     %s\n             windows: %s' % (a, b), flush=True)
    ended(checks, stage, exited, lines, work)
    return same, len(ref)


def stage_mod(checks, binary, arch, out, inputs):
    stage = 'mod-' + arch
    work = new_workdir(out, stage, inputs, mods=('snap64_zz_hooktest.nrm',))
    lines, exited, took = run_game(binary, arch, work, {}, 240,
                                   lambda ls: any('[hooktest] dmaCopy hook fired' in l for l in ls), 10.0)
    keep_log(work, lines)
    fired = [l for l in lines if l.startswith('[hooktest]')]
    kinds = sorted(set(re.sub(r' hook fired.*', '', l) for l in fired))
    checks.add(stage, any('dmaCopy' in k for k in kinds),
               'the mod\'s hooks ran: %s (%d lines in %.0f s)' % (', '.join(kinds) or 'none', len(fired), took))
    refused = [l for l in lines if ('[SNAP-MODS]' in l or '[Mods]' in l) and ('fail' in l.lower() or 'refus' in l.lower())]
    if refused:
        print('           mod messages: %s' % ' | '.join(refused[:4]), flush=True)
    ended(checks, stage, exited, lines, work)


def main():
    ap = argparse.ArgumentParser(description='The macOS bundle, run on a Mac with no one at it.')
    ap.add_argument('app', help='Snap64Recomp.app')
    ap.add_argument('--inputs', required=True, help='the ROM, the replay\'s save and the test mod (see the docstring)')
    ap.add_argument('--out', required=True, help='where each stage\'s folder and log go')
    ap.add_argument('--arch', default='arm64', choices=['arm64', 'x86_64'])
    ap.add_argument('--stages', default='boot,validated,score,mod')
    args = ap.parse_args()

    app = pathlib.Path(args.app).resolve()
    binary = app / 'Contents' / 'MacOS' / 'Snap64Recomp'
    inputs = pathlib.Path(args.inputs).resolve()
    out = pathlib.Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    if not binary.is_file():
        print('no executable at %s' % binary)
        return 1
    started = time.time()
    checks = Checks()
    stages = args.stages.split(',')
    if 'boot' in stages:
        stage_boot(checks, binary, args.arch, out, inputs, False)
    if 'validated' in stages:
        stage_boot(checks, binary, args.arch, out, inputs, True)
    if 'score' in stages:
        stage_score(checks, binary, args.arch, out, inputs)
    if 'mod' in stages:
        stage_mod(checks, binary, args.arch, out, inputs)

    reports = []
    if DIAG.is_dir():
        for f in DIAG.iterdir():
            if f.name.startswith('Snap64Recomp') and f.stat().st_mtime >= started - 5:
                shutil.copyfile(f, out / f.name)
                reports.append(f.name)
    checks.add('crashes', not reports, 'crash reports from macOS: %s' % (', '.join(reports) or 'none'))

    failed = checks.failed()
    summary = ['### macOS %s: %d checks, %d failed' % (args.arch, len(checks.rows), len(failed)), '',
               '| | stage | result |', '|---|---|---|']
    for stage, ok, text in checks.rows:
        summary.append('| %s | %s | %s |' % ('PASS' if ok else 'FAIL', stage, text.replace('|', '/')))
    step = os.environ.get('GITHUB_STEP_SUMMARY')
    if step:
        with open(step, 'a', encoding='utf-8') as f:
            f.write('\n'.join(summary) + '\n\n')
    print('\n%d checks, %d failed' % (len(checks.rows), len(failed)))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
