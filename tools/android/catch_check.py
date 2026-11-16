"""Issue 39's round, checked on a device or the emulator with the tree's own Android check helpers: a graphics
setting the device cannot carry must never end every start the same way.

  plain      the app as it is: the log names the anti-aliasing levels the device offers, nothing is held and
             no render target is lost;
  fit        the file asks 8x and SNAP_MSAA_TEST plays a device that offers less: the level is held at the
             start, said, and written back;
  catch      Super Sampling 1x and the device's highest level asked, SNAP_TARGET_FAIL_TEST=1 fails the first
             color target: the port says which level it lowered, marks it, saves one level down and ends by
             itself (until 1.1.7 it lowered nothing and every start ended the same way);
  next       asked the failed level again, the next start holds below the mark and writes that back;
  color      with anti-aliasing off and Color Depth High, the same failure sets Color Depth to Standard;
  scale      with Color Depth Standard, SNAP_TARGET_FAIL_TEST=480 fails the first target at the screen's
             scale: Render Scale is set to half the scale in force;
  floor      with Render Scale at 1x nothing is left: the message says so and no setting changes;
  safestart  two starts ended at their first display list by SNAP_EARLY_DEATH_TEST with anti-aliasing raised
             are counted in snap64.starting; the third, answered "defaults", says so, saves anti-aliasing
             off, and the mark is gone once it has drawn.

    py -3.13 -u catch_check.py <out dir> --apk <debug apk> --rom <pokemonsnap.z64> --adb <adb>
                               [--serial <device>] [--label 1.1.8-test1]
                               [--avd <name> [--window] [--emulator <emulator>]]

With --avd the virtual device is booted first and shut down at the end, as tools/android_check.py does.
"""
import argparse
import json
import os
import pathlib
import re
import subprocess
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
TOOLS = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import android_check as ac  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument('out')
ap.add_argument('--apk', required=True)
ap.add_argument('--rom', required=True)
ap.add_argument('--adb', required=True)
ap.add_argument('--serial')
ap.add_argument('--label', default='', help='the version the app must say, when given')
ap.add_argument('--avd', help='boot this virtual device first, and shut it down at the end')
ap.add_argument('--window', action='store_true', help='show the emulator\'s window')
sdk = pathlib.Path(os.environ.get('ANDROID_HOME', pathlib.Path.home() / 'android-sdk'))
ap.add_argument('--emulator', default=str(sdk / 'emulator' / ('emulator' + ('.exe' if os.name == 'nt' else ''))),
                help='the emulator binary, for --avd')
a = ap.parse_args()
out = pathlib.Path(a.out).resolve()
out.mkdir(parents=True, exist_ok=True)
a.out = str(out)

OFFERS = '[SNAP-CFG] anti-aliasing the card offers for its render targets:'
LOST = ('[SNAP-GPU] ', '[SNAP-AV]', '[SNAP-HANG]')
SAID = '[SNAP-GPU] The card could not make a color render target'
STAGES = ('catch-plain', 'catch-fit', 'catch-msaa', 'catch-color', 'catch-scale', 'catch-floor', 'catch-safestart')


def first(lines, text):
    return next((l for l in lines if text in l), None)


def title(lines):
    return any('menu font harvested' in l for l in lines)


def never(lines):
    return False


def name_of(level):
    return '%dx' % level if level >= 2 else 'off'


def below(level):
    return 4 if level >= 8 else 2 if level >= 4 else 0


def main():
    emulator = ac.boot_emulator(a) if a.avd else None
    dev = ac.Device(a.adb, a.serial)
    (out / 'summary.txt').write_text('', encoding='utf-8')
    checks = ac.Checks(out / 'summary.txt')
    try:
        if not dev.online():
            print('no device')
            return 1
        dev.run(['logcat', '-G', '16M'])
        said = dev.run(['install', '-r', a.apk], timeout=600).strip().splitlines()[-1]
        if 'INSTALL_FAILED_UPDATE_INCOMPATIBLE' in said:
            print('install: %s; the installed app is removed first' % said, flush=True)
            dev.run(['uninstall', ac.PKG], timeout=120)
            said = dev.run(['install', a.apk], timeout=600).strip().splitlines()[-1]
        print('install: %s' % said, flush=True)
        if 'Success' not in said:
            checks.add('install', False, said)
            return 1
        dev.sh('appops set %s MANAGE_EXTERNAL_STORAGE allow' % ac.PKG)
        dev.sh('appops set --uid %s MANAGE_EXTERNAL_STORAGE allow' % ac.PKG)
        run_stages(dev, checks)
        dev.stop()
        for name in STAGES:
            dev.sh('rm -rf %s/%s' % (ac.ROOT, name))
    finally:
        if emulator is not None:
            dev.run(['emu', 'kill'])
            try:
                emulator.wait(timeout=60)
            except subprocess.TimeoutExpired:
                emulator.kill()
    bad = checks.failed()
    print('%d checks, %d failed' % (len(checks.rows), len(bad)))
    return 1 if bad else 0


def run_stages(dev, checks):
    def settings_into(stage, values):
        """Writes the settings file of the stage's game folder: only these keys, the rest at their defaults."""
        local = out / ('%s-settings.json' % stage.name)
        local.write_text(json.dumps(values, indent=2), encoding='utf-8')
        dev.push(local, stage.folder + '/snapsettings.json')

    def saved(stage):
        try:
            return json.loads((dev.cat(stage.folder + '/snapsettings.json') or b'').decode('utf-8'))
        except ValueError:
            return {}

    def mark_of(stage, name):
        return (dev.cat(stage.folder + '/' + name) or b'').decode('utf-8', errors='replace').strip()

    def lost(lines):
        return next((l for l in lines if l.startswith(LOST)), None)

    def fail_once(name, values, least='1'):
        """A fresh folder, these settings, and a start whose color target fails: (stage, the port's line)."""
        s = ac.Stage(dev, out, name)
        s.fresh([(a.rom, 'pokemonsnap.z64')])
        settings_into(s, values)
        s.run({'SNAP_TARGET_FAIL_TEST': least, 'SNAP_NO_BOX': '1'}, never, 0.0, 240, keep=False)
        return s, (first(s.lines, SAID) or '')

    def ended(s):
        return 'ended by itself' if s.ended else 'did NOT end by itself'

    # 1. As it is: what the device offers is in the log; nothing held, nothing lost.
    s = ac.Stage(dev, out, 'catch-plain')
    s.fresh([(a.rom, 'pokemonsnap.z64')])
    s.run({}, title, 3.0, 400, keep=False)
    version = first(s.lines, '[SNAP] Snap64 Recomp ')
    checks.add('plain', version is not None and a.label in version, version or 'no version line')
    checks.add('plain', first(s.lines, '[SNAP] built from commit') is not None,
               first(s.lines, '[SNAP] built from commit') or 'no build line')
    offers = first(s.lines, OFFERS)
    checks.add('plain', offers is not None, offers or 'no "%s" line' % OFFERS)
    held = next((l for l in s.lines if l.startswith('[SNAP-CFG] anti-aliasing') and ': held to ' in l), None)
    checks.add('plain', held is None and lost(s.lines) is None,
               held or lost(s.lines) or 'nothing was held and no render target was lost')
    s.judge_end(checks)
    levels = [n for n in (8, 4, 2) if offers and (' %dx' % n) in offers.split(':', 1)[1].split('(')[0]]
    top = levels[0] if levels else 0
    if top == 0:
        checks.add('fit', False, 'the device offers no anti-aliasing level, so the fit and the catch cannot be exercised here')
        return

    # 2. The file asks 8x on a device played as offering less: held, said, written back.
    play = 4 if top >= 4 else 2
    s = ac.Stage(dev, out, 'catch-fit')
    s.fresh([(a.rom, 'pokemonsnap.z64')])
    settings_into(s, {'downsample': 1, 'msaa': 8})
    s.run({'SNAP_MSAA_TEST': str(play)}, title, 3.0, 400, keep=False)
    faked = first(s.lines, '[SNAP-CFG] SNAP_MSAA_TEST: the card offers anti-aliasing up to %dx' % play)
    line = first(s.lines, '[SNAP-CFG] anti-aliasing 8x is not offered by this card')
    after = saved(s).get('msaa')
    checks.add('fit', faked is not None and line is not None and line.endswith(': held to %dx' % play)
               and after == play and lost(s.lines) is None,
               (line.split('] ', 1)[1] if line else 'the start never held the level')
               + ('' if faked else '; SNAP_MSAA_TEST was not taken') + '; written back as %s' % after
               + ('; ' + lost(s.lines) if lost(s.lines) else ''))
    s.judge_end(checks)

    # 3. The device's highest level, Super Sampling 1x, and the first color target fails.
    low = below(top)
    s, line = fail_once('catch-msaa', {'downsample': 1, 'msaa': top})
    want = 'with Anti-Aliasing at %dx. Anti-Aliasing is set to %s now.' % (top, ('%dx' % low) if low else 'Off')
    now = saved(s)
    mark = mark_of(s, 'snap64.gpu_failed')
    checks.add('catch', want in line and s.ended and mark.startswith('downsample 1 msaa %d ' % top)
               and now.get('msaa') == low and now.get('downsample') == 1,
               ('said; ' if want in line else ('never said; ' if not line else 'said something else ("%s"); ' % line[:150]))
               + ended(s) + '; mark "%s"; saved anti-aliasing %s, super sampling %s'
               % (mark, now.get('msaa'), now.get('downsample')))

    # 4. Asked the failed level again, in the same folder: held below the mark.
    settings_into(s, {'downsample': 1, 'msaa': top})
    s.run({}, title, 3.0, 400, keep=False, tag='-next')
    line = first(s.lines, '[SNAP-CFG] anti-aliasing %dx failed on this card before' % top)
    after = saved(s).get('msaa')
    checks.add('next', line is not None and line.endswith(': held to %s' % name_of(low)) and after == low
               and lost(s.lines) is None,
               (line.split('] ', 1)[1] if line else 'the next start did not hold below the mark')
               + '; written back as %s' % after + ('; ' + lost(s.lines) if lost(s.lines) else ''))
    s.judge_end(checks)

    # 5. Anti-aliasing off, Color Depth High: set to Standard.
    s, line = fail_once('catch-color', {'downsample': 1, 'msaa': 0, 'color_depth': 2, 'resolution_scale': 0})
    now = saved(s)
    want = 'at 16 bits a channel. Color Depth is set to Standard now.'
    checks.add('color', want in line and s.ended and now.get('color_depth') == 1 and now.get('msaa') == 0,
               ('said; ' if want in line else ('never said; ' if not line else 'said something else ("%s"); ' % line[:150]))
               + ended(s) + '; saved color depth %s, anti-aliasing %s' % (now.get('color_depth'), now.get('msaa')))

    # 6. Color Depth Standard: the first target at the screen's scale fails, Render Scale is halved.
    s, line = fail_once('catch-scale', {'downsample': 1, 'msaa': 0, 'color_depth': 1, 'resolution_scale': 0}, least='480')
    now = saved(s)
    m = re.search(r'Render Scale is set to (\d+)x now\..*the scale in force was (\d+)x', line)
    held, was = (int(m.group(1)), int(m.group(2))) if m else (None, None)
    checks.add('scale', bool(m) and was > 1 and held == max(1, was // 2) and s.ended and now.get('resolution_scale') == held,
               (('said: %dx in force, set to %dx; ' % (was, held)) if m
                else ('never said; ' if not line else 'said something else ("%s"); ' % line[:150]))
               + ended(s) + '; saved render scale %s' % now.get('resolution_scale'))

    # 7. Render Scale 1x: nothing is left, and the message says so.
    floor = {'downsample': 1, 'msaa': 0, 'color_depth': 1, 'resolution_scale': 1}
    s, line = fail_once('catch-floor', floor)
    now = saved(s)
    want = 'are already at their lowest, so nothing was changed.'
    same = all(now.get(k) == v for k, v in floor.items())
    mark = mark_of(s, 'snap64.gpu_failed')
    checks.add('floor', want in line and s.ended and same and not mark,
               ('said; ' if want in line else ('never said; ' if not line else 'said something else ("%s"); ' % line[:150]))
               + ended(s) + ('; the four settings unchanged' if same else '; a setting CHANGED: %s' % now)
               + ('; a mark was written' if mark else '; no mark'))

    # 8. Two starts that die before their first frames, a setting raised; the third is asked.
    s = ac.Stage(dev, out, 'catch-safestart')
    s.fresh([(a.rom, 'pokemonsnap.z64')])
    settings_into(s, {'downsample': 1, 'msaa': top})
    counts = []
    for tag in ('-1', '-2'):
        s.run({'SNAP_EARLY_DEATH_TEST': '1', 'SNAP_SAFE_GFX_ANSWER': 'keep'}, never, 0.0, 240, keep=False, tag=tag)
        counts.append((s.ended, mark_of(s, 'snap64.starting')))
    checks.add('safestart', counts == [(True, 'starts 1'), (True, 'starts 2')],
               'two starts at their first display list: %s with the mark "%s", then %s with "%s"'
               % ('ended' if counts[0][0] else 'did NOT end', counts[0][1], 'ended' if counts[1][0] else 'did NOT end', counts[1][1]))
    s.run({'SNAP_SAFE_GFX_ANSWER': 'defaults'}, title, 3.0, 400, keep=False, tag='-3')
    found = first(s.lines, '[SNAP-CFG] start: the last 2 starts ended before their first frames')
    answered = first(s.lines, '[SNAP-CFG] start: SNAP_SAFE_GFX_ANSWER=defaults answers the question')
    lowered = first(s.lines, '[SNAP-CFG] start: Super Sampling 1x, Anti-Aliasing off')
    after = saved(s).get('msaa')
    gone = not dev.exists(s.folder + '/snap64.starting')
    checks.add('safestart', found is not None and answered is not None and lowered is not None and after == 0 and gone
               and lost(s.lines) is None,
               ('found the two starts; ' if found else 'did NOT find the two starts; ')
               + ('answered; ' if answered else 'not answered; ')
               + (lowered.split('] ', 1)[1] if lowered else 'nothing lowered')
               + '; saved anti-aliasing %s' % after
               + ('; the mark cleared once it drew' if gone else '; the mark is still there')
               + ('; ' + lost(s.lines) if lost(s.lines) else ''))
    s.judge_end(checks)


if __name__ == '__main__':
    sys.exit(main())
