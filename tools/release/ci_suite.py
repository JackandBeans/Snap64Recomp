"""GitHub's build, fetched and checked here: the six archives of a release workflow run downloaded, their
archives and sidecars laid in <ci dir>/archives-<version> (sidecars normalized to LF), each archive hashed
against its sidecar, the Windows zip unpacked into <ci dir>/Snap64Recomp-<version>-win64 with the suite's
inputs copied in from the development folder (the ROM, the replays' save, the four replays, the settings,
the example mods), then the release suite, the station and the package check on that executable. Both
suites open the game window; run on a quiet machine.

    tools/release/ci_suite.py <version> <run id> [--no-download] [--no-suite]

Environment: SNAP64_CI_DIR (default ~/snap64-ci-suite), SNAP64_DEV (default build-win/Release),
SNAP64_REPO (default from origin)."""
import glob
import hashlib
import io
import os
import shutil
import subprocess
import sys
import zipfile

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
ROOT = subprocess.run(['git', 'rev-parse', '--show-toplevel'], capture_output=True, text=True).stdout.strip()
if len(sys.argv) < 3:
    sys.exit(__doc__)
V, RUN = sys.argv[1], sys.argv[2]
CI = os.path.expanduser(os.environ.get('SNAP64_CI_DIR', '~/snap64-ci-suite')).replace('\\', '/').rstrip('/') + '/'
DEV = (os.environ.get('SNAP64_DEV') or os.path.join(ROOT, 'build-win', 'Release')).replace('\\', '/').rstrip('/') + '/'
REPO = os.environ.get('SNAP64_REPO') or subprocess.run(['git', 'remote', 'get-url', 'origin'], capture_output=True, text=True).stdout.strip().split('github.com')[-1].lstrip(':/').removesuffix('.git')
RAW = CI + 'artifacts-%s/' % V
ARCH = CI + 'archives-%s/' % V
FOLDER = CI + 'Snap64Recomp-%s-win64/' % V
OUT = CI + 'ci_suite_%s_' % V
TOOLS = os.path.join(ROOT, 'tools') + '/'


def sha(path):
    return hashlib.sha256(open(path, 'rb').read()).hexdigest()


if '--no-download' not in sys.argv:
    shutil.rmtree(RAW, ignore_errors=True)
    os.makedirs(RAW)
    r = subprocess.run(['gh', 'run', 'download', RUN, '-R', REPO, '-D', RAW], capture_output=True)
    print('download:', r.returncode, r.stderr.decode('utf-8', 'replace')[:300])
    os.makedirs(ARCH, exist_ok=True)
    for f in glob.glob(RAW + '**/*', recursive=True):
        if os.path.isfile(f) and (f.endswith(('.zip', '.tar.gz', '.flatpak', '.apk')) or f.endswith('.sha256')):
            dst = ARCH + os.path.basename(f)
            if f.endswith('.sha256'):
                open(dst, 'wb').write(open(f, 'rb').read().replace(b'\r\n', b'\n'))
            else:
                shutil.copyfile(f, dst)
files = sorted(os.listdir(ARCH))
print('archives-%s: %d files' % (V, len(files)))
ok = True
for f in files:
    if f.endswith('.sha256'):
        continue
    side = ARCH + f + '.sha256'
    want = io.open(side, encoding='utf-8').read().split()[0] if os.path.exists(side) else '(no sidecar)'
    got = sha(ARCH + f)
    ok = ok and (want == got)
    print('  %-46s %10d  %s  %s' % (f, os.path.getsize(ARCH + f), got[:16], 'matches its sidecar' if want == got else 'DIFFERS: ' + want[:16]))
print('sidecars:', 'ALL MATCH' if ok else 'SOMETHING DIFFERS')
# The APK comes signed when the release key's secrets are on GitHub, and
# otherwise unsigned, to be signed here (tools/release/sign_apk.sh); after
# that both are in the folder, and either is the one Android archive.
kinds = set(f.replace('-android-unsigned.apk', '-android.apk') for f in files if not f.endswith('.sha256'))
if len(kinds) != 6:
    raise SystemExit('expected six archives')
apk = 'Snap64Recomp-%s-android.apk' % V
if apk not in files:
    # The script runs where the SDK's build-tools are: under WSL here, which
    # sees C: as /mnt/c.
    wsl = lambda p: ('/mnt/' + p[0].lower() + p[2:]) if (len(p) > 2 and p[1] == ':') else p
    print('the APK is unsigned: sign it before the page is made, under WSL or on Linux (BUILDING.md, step 16):\n'
          '  bash %s/tools/release/sign_apk.sh %sSnap64Recomp-%s-android-unsigned.apk <keystore>'
          % (wsl(ROOT.replace('\\', '/')), wsl(ARCH), V))

win = [f for f in files if f.endswith('-win64.zip')][0]
shutil.rmtree(FOLDER, ignore_errors=True)
with zipfile.ZipFile(ARCH + win) as z:
    top = z.namelist()[0].split('/')[0]
    z.extractall(CI)
if CI + top + '/' != FOLDER:
    os.rename(CI + top, FOLDER.rstrip('/'))
for name in ('pokemonsnap.z64', 'snapsettings.json', 'beach.inputs', 'eval.inputs', 'menu_title.inputs', 'station.inputs'):
    shutil.copyfile(DEV + name, FOLDER + name)
os.makedirs(FOLDER + 'saves', exist_ok=True)
shutil.copyfile(DEV + 'saves/pokemonsnap.bin', FOLDER + 'saves/pokemonsnap.bin')
os.makedirs(FOLDER + 'mods', exist_ok=True)
for f in glob.glob(DEV + 'mods/*.nrm'):
    shutil.copyfile(f, FOLDER + 'mods/' + os.path.basename(f))
exe = sha(FOLDER + 'Snap64Recomp.exe')
print('folder:', FOLDER, 'exe', exe[:16], 'map', os.path.exists(FOLDER + 'Snap64Recomp.map'))
if os.path.exists(FOLDER + 'Snap64Recomp.map'):
    # The words of unannounced work, from .claude/private-words.txt on the
    # release machine (never in the tree); a hit inside SNAP_VRAM_TEST is a
    # false alarm. Words under four letters are for the posts' text only:
    # lowercased, they would match all through a map.
    m = open(FOLDER + 'Snap64Recomp.map', 'rb').read().lower()
    try:
        words = [w.strip().lower().encode() for w in io.open(os.path.join(ROOT, '.claude', 'private-words.txt'),
                                                              encoding='utf-8') if len(w.strip()) >= 4 and not w.startswith('#')]
        print('private words in the map:', sum(m.count(w) for w in words), '(%d words looked for)' % len(words))
    except OSError:
        print('!! .claude/private-words.txt is missing: the map was not scanned for the private words')

if '--no-suite' in sys.argv:
    raise SystemExit(0)
for label, args in (('suite', []), ('station', ['--only', 'station']), ('package', ['--only', 'package', '--zip', ARCH + win])):
    with open(OUT + label + '.log', 'w') as log:
        r = subprocess.run([sys.executable, TOOLS + 'release_check.py', FOLDER] + args, stdout=log, stderr=subprocess.STDOUT)
    text = io.open(OUT + label + '.log', encoding='utf-8', errors='replace').read()
    tail = [l for l in text.splitlines() if l.strip()][-1:]
    fails = [l for l in text.splitlines() if l.startswith('FAIL')]
    print('%s: exit %d | %s | %s' % (label, r.returncode, tail[0] if tail else '?', ('FAILS: ' + '; '.join(fails)) if fails else 'no failures'))
print('logs:', OUT + '{suite,station,package}.log')
