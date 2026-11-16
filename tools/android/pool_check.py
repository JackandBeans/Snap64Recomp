"""The descriptor pool fix (issue 36), checked on a device or the emulator with the tree's own Android check
helpers. SNAP_VK_POOL_TEST plays the Adreno driver's accounting (a variable-count descriptor set is refused
while its pool holds less than its layout declares), so the fix runs wherever the check does.

  plain     the app as it is: what the driver says of descriptor indexing is logged, nothing is refused,
            three frames with a picture;
  retry     SNAP_VK_POOL_TEST=1, the accounting played on a driver the fix does not know: every refusal is
            answered by a retry that allocated, none is left failed, and the settled frame is the plain run's;
  declared  SNAP_VK_POOL_TEST=2, the device also treated as Qualcomm's, which is the Thor's own path: the
            pools hold the declared count from the start, so nothing may be refused at all;
  score     the tree's own scoring ride (tools/android_check.py, stage_score) on the Thor's path: the photos
            are scored from frames the renderer drew and read back.

The first two captured frames fall in the boot's fades and differ from run to run on any build (the released
1.1.7 gave 53, 85 and 122 for the second one in three runs), so only the third, settled one is compared.

    py -3.13 -u pool_check.py <out dir> --apk <debug apk> --rom <pokemonsnap.z64> --save <the tapes' save>
                              --adb <adb> [--serial <device>] [--label 1.1.7-test2]
"""
import argparse
import pathlib
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
TOOLS = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import android_check as ac  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument('out')
ap.add_argument('--apk', required=True)
ap.add_argument('--rom', required=True)
ap.add_argument('--save', required=True)
ap.add_argument('--adb', required=True)
ap.add_argument('--serial')
ap.add_argument('--label', default='', help='the version the app must say, when given')
a = ap.parse_args()
out = pathlib.Path(a.out)
out.mkdir(parents=True, exist_ok=True)
dev = ac.Device(a.adb, a.serial)
(out / 'summary.txt').write_text('', encoding='utf-8')
checks = ac.Checks(out / 'summary.txt')
if not dev.online():
    print('no device')
    sys.exit(1)
dev.run(['logcat', '-G', '16M'])
said = dev.run(['install', '-r', a.apk], timeout=600).strip().splitlines()[-1]
if 'INSTALL_FAILED_UPDATE_INCOMPATIBLE' in said:
    print('install: %s; the installed app is removed first' % said, flush=True)
    dev.run(['uninstall', ac.PKG], timeout=120)
    said = dev.run(['install', a.apk], timeout=600).strip().splitlines()[-1]
print('install: %s' % said, flush=True)
if 'Success' not in said:
    checks.add('install', False, said)
    sys.exit(1)
dev.sh('appops set %s MANAGE_EXTERNAL_STORAGE allow' % ac.PKG)
dev.sh('appops set --uid %s MANAGE_EXTERNAL_STORAGE allow' % ac.PKG)

LIMITS = '[SNAP-VK] descriptor indexing:'
REFUSED = '[SNAP-VK] descriptor set refused'
RETRIED = '[SNAP-VK] descriptor set: the retry at the declared count was allocated'
DECLARED = '[SNAP-VK] descriptor pools: a variable-count set\'s pool holds its layout\'s declared count'


def boot(name, extras):
    s = ac.Stage(dev, out, name)
    s.fresh([(a.rom, 'pokemonsnap.z64')])
    env = {'SNAP_PCAP_ATFRAME': '300,900,1500', 'SNAP_PCAP_BURST': '1', 'SNAP_STATS': '1'}
    env.update(extras)
    s.run(env, lambda lines: ac.count(lines, '[SNAP-PCAP] wrote') >= 3, 3.0, 600)
    means = [ac.bmp_mean(f) for f in ac.frames_of(s)]
    return s, [m for m in means if m is not None]


def first(lines, text):
    return next((l for l in lines if text in l), None)


def none_failed(stage, lines):
    left = first(lines, 'vkAllocateDescriptorSets failed') or first(lines, 'refused again')
    checks.add(stage, left is None, left or 'no allocation was left failed')


def pictures(stage, means, plain):
    lit = [m for m in means if m > 12]
    checks.add(stage, len(lit) >= 3, '%d frames with a picture (mean luminance %s)'
               % (len(lit), ', '.join('%.0f' % m for m in means) or '-'))
    if plain is not None:
        same = len(means) >= 3 and len(plain) >= 3 and abs(means[2] - plain[2]) <= 2.0
        checks.add(stage, same, 'the settled third frame beside the plain run\'s: %s against %s'
                   % ('%.1f' % means[2] if len(means) >= 3 else '-', '%.1f' % plain[2] if len(plain) >= 3 else '-'))


# 1. As it is: the driver's word on descriptor indexing, nothing refused, a picture.
s, plain = boot('pool-plain', {})
version = first(s.lines, '[SNAP] Snap64 Recomp ')
checks.add('plain', version is not None and a.label in version, version or 'no version line')
checks.add('plain', first(s.lines, '[SNAP] built from commit') is not None, first(s.lines, '[SNAP] built from commit') or 'no build line')
checks.add('plain', first(s.lines, LIMITS) is not None, first(s.lines, LIMITS) or 'no "%s" line' % LIMITS)
checks.add('plain', first(s.lines, REFUSED) is None, first(s.lines, REFUSED) or 'no descriptor set was refused')
pictures('plain', plain, None)
s.judge_end(checks)

# 2. The accounting played on a driver the fix does not know: every refusal retried.
s, means = boot('pool-retry', {'SNAP_VK_POOL_TEST': '1'})
checks.add('retry', first(s.lines, '[SNAP-VK] SNAP_VK_POOL_TEST') is not None,
           first(s.lines, '[SNAP-VK] SNAP_VK_POOL_TEST') or 'the switch said nothing')
refused = [l for l in s.lines if l.startswith(REFUSED)]
retried = [l for l in s.lines if l.startswith(RETRIED)]
checks.add('retry', len(refused) >= 1 and len(refused) == len(retried),
           '%d descriptor sets refused, %d retries allocated; the first: %s'
           % (len(refused), len(retried), refused[0][:210] if refused else '-'))
none_failed('retry', s.lines)
pictures('retry', means, plain)
s.judge_end(checks)

# 3. The Thor's own path: the device treated as Qualcomm's, the accounting played, nothing refused.
s, means = boot('pool-declared', {'SNAP_VK_POOL_TEST': '2'})
checks.add('declared', first(s.lines, DECLARED) is not None, first(s.lines, DECLARED) or 'no "descriptor pools" line')
checks.add('declared', first(s.lines, REFUSED) is None, first(s.lines, REFUSED) or 'no descriptor set was refused')
none_failed('declared', s.lines)
pictures('declared', means, plain)
s.judge_end(checks)

# 4. The scoring ride on the Thor's path, by the tree's own stage: every start gets the switch.
plain_start = ac.Device.start
ac.Device.start = lambda self, extras: plain_start(self, dict(extras, SNAP_VK_POOL_TEST='2'))
ac.stage_score(checks, dev, out, argparse.Namespace(rom=a.rom, save=a.save))
ac.Device.start = plain_start
ride = (out / 'score' / 'snap64.log').read_text(encoding='utf-8', errors='replace').splitlines()
checks.add('score', first(ride, DECLARED) is not None and first(ride, REFUSED) is None,
           'the ride ran on the declared-count pools with %d sets refused' % len([l for l in ride if l.startswith(REFUSED)]))
none_failed('score', ride)

bad = checks.failed()
print('%d checks, %d failed' % (len(checks.rows), len(bad)))
sys.exit(1 if bad else 0)
