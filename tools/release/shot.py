"""The release's title picture by the recipe of every release: a scratch data folder made from the
development folder (its ROM, the replays' save and its settings, with fullscreen on and MSAA 8x),
`idle_title.inputs` replayed with one present captured full size at reading 1140 (1920x1440, the 4:3
picture), the game closed two seconds after the capture is written, and the capture cropped to
1920x1439 as every release image is. The result is compared with the previous title picture: only the
version digit should differ. The game's window opens for about a minute.

    tools/release/shot.py <version> <letter> [<previous title picture>]

writes docs/screenshots/01<letter>-title-<ver>.png, where <ver> is the version without dots.
Environment: SNAP64_DEV (default build-win/Release), SNAP64_CI_DIR (default ~/snap64-ci-suite; the
scratch data folder is made under it)."""
import glob
import json
import os
import shutil
import subprocess
import sys
import time

from PIL import Image, ImageChops

if len(sys.argv) < 3:
    sys.exit(__doc__)
V, LETTER = sys.argv[1], sys.argv[2]
ROOT = subprocess.run(['git', 'rev-parse', '--show-toplevel'], capture_output=True, text=True).stdout.strip().replace('\\', '/')
DEV = (os.environ.get('SNAP64_DEV') or ROOT + '/build-win/Release').replace('\\', '/').rstrip('/')
CI = os.path.expanduser(os.environ.get('SNAP64_CI_DIR', '~/snap64-ci-suite')).replace('\\', '/').rstrip('/')
DATA = CI + '/shot-data'
CWD = CI + '/shot-run'
OUT = '%s/docs/screenshots/01%s-title-%s.png' % (ROOT, LETTER, V.replace('.', ''))

shutil.rmtree(CWD, ignore_errors=True)
os.makedirs(CWD)
os.makedirs(DATA + '/saves', exist_ok=True)
for name in ('pokemonsnap.z64',):
    if not os.path.exists(DATA + '/' + name):
        shutil.copyfile(DEV + '/' + name, DATA + '/' + name)
shutil.copyfile(DEV + '/saves/pokemonsnap.bin', DATA + '/saves/pokemonsnap.bin')
settings = json.load(open(DEV + '/snapsettings.json', encoding='utf-8'))
settings['fullscreen'] = True
settings['msaa'] = 8
json.dump(settings, open(DATA + '/snapsettings.json', 'w', encoding='utf-8'), indent=2)
for stale in ('snap64.running', 'snap64.gpu_failed'):
    if os.path.exists(DATA + '/' + stale):
        os.remove(DATA + '/' + stale)

env = dict(os.environ, SNAP_DATA_DIR=DATA, SNAP_REPLAY=DEV + '/idle_title.inputs', SNAP_PCAP_FULL='1', SNAP_MUTE='1',
           SNAP_PCAP_AT='1140', SNAP_PCAP_BURST='1', SNAP_SAFE_START_ANSWER='keep')
log = open(CWD + '/run.log', 'w')
p = subprocess.Popen([DEV + '/Snap64Recomp.exe'], cwd=CWD, env=env, stdout=log, stderr=subprocess.STDOUT)
start = time.time()
seen = None
while p.poll() is None and (time.time() - start) < 90:
    time.sleep(0.5)
    if glob.glob(CWD + '/snap_frame_dumps/*.bmp') and seen is None:
        seen = time.time()
    if seen is not None and (time.time() - seen) > 2.0:
        break
if p.poll() is None:
    p.kill()
p.wait(timeout=30)
log.close()
files = sorted(glob.glob(CWD + '/snap_frame_dumps/*.bmp'))
if not files:
    sys.exit('no capture; see ' + CWD + '/run.log')
im = Image.open(files[0]).convert('RGB')
print('capture', os.path.basename(files[0]), im.size)
assert im.size == (1920, 1440), 'not the 4:3 full capture: %s' % (im.size,)
im = im.crop((0, 0, 1920, 1439))
im.save(OUT, optimize=True)
print('written', OUT)
if len(sys.argv) > 3:
    old = Image.open(sys.argv[3]).convert('RGB')
    box = ImageChops.difference(old, im).convert('L').point(lambda v: 255 if v > 24 else 0).getbbox()
    print('differs from %s inside %s (the version digit sits near the bottom)' % (os.path.basename(sys.argv[3]), box))
