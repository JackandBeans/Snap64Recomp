"""The playtest folder takes a build: every file of a win64 zip except the folders that hold the author's
own things (cache, mods, texture_packs; the zip has no saves or settings), the executable and map it
replaces kept under a name first, both hashed, the copy checked against the zip.

    tools/release/playtest.py <win64 zip> --dest <playtest folder> [--keep-as <name>]

The folder can also come from the environment variable SNAP64_PLAYTEST. A kept name defaults to
Snap64Recomp-<hash8>-<mmmdd>."""
import hashlib
import io
import os
import shutil
import sys
import time
import zipfile

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
args = sys.argv[1:]
if not args:
    sys.exit(__doc__)
ZIP = args[0]
DST = (args[args.index('--dest') + 1] if '--dest' in args else os.environ.get('SNAP64_PLAYTEST', '')).rstrip('/\\')
if not DST or not os.path.isdir(DST):
    sys.exit('the playtest folder is not given (--dest or SNAP64_PLAYTEST) or does not exist: %r' % DST)
DST += '/'
KEEP = ('cache/', 'mods/', 'texture_packs/')


def sha(path):
    return hashlib.sha256(open(path, 'rb').read()).hexdigest()


old = sha(DST + 'Snap64Recomp.exe') if os.path.exists(DST + 'Snap64Recomp.exe') else None
print('the playtest executable now:', old[:16] if old else '(none)')
if old:
    kept = args[args.index('--keep-as') + 1] if '--keep-as' in args else 'Snap64Recomp-%s-%s' % (old[:8], time.strftime('%b%d').lower())
    for ext in ('exe', 'map'):
        if os.path.exists(DST + 'Snap64Recomp.' + ext) and not os.path.exists(DST + kept + '.' + ext):
            shutil.copy2(DST + 'Snap64Recomp.' + ext, DST + kept + '.' + ext)
            print('kept as', kept + '.' + ext)

z = zipfile.ZipFile(ZIP)
copied, skipped = 0, []
for info in z.infolist():
    if info.is_dir():
        continue
    rel = info.filename.split('/', 1)[1]
    if rel.startswith(KEEP):
        skipped.append(rel)
        continue
    out = DST + rel
    os.makedirs(os.path.dirname(out), exist_ok=True)
    data = z.read(info)
    with open(out, 'wb') as f:
        f.write(data)
    assert sha(out) == hashlib.sha256(data).hexdigest()
    copied += 1
print('copied %d files; left alone: %s' % (copied, skipped))
top = z.namelist()[0].split('/')[0]
new = sha(DST + 'Snap64Recomp.exe')
print('the playtest executable is now:', new[:16], '| same as the zip:', new == hashlib.sha256(z.read(top + '/Snap64Recomp.exe')).hexdigest())
for name in ('saves', 'snapsettings.json', 'mods', 'mods.json', 'pokemonsnap.z64'):
    print('  still there:', name, os.path.exists(DST + name))
