"""The crash record (issue 36), checked on a device or the emulator with the tree's own Android check helpers.
SNAP_CRASH_TEST makes the thread that begins its n-th named Vulkan call crash there, so what the app writes when
it dies, and what it copies from Android's own records at the next start, can be read back and compared.

  segv        the app writes to address 0 at a Vulkan call: its own lines name the signal, the thread, that call
              and where it stopped in libmain.so;
  segv-after  the next start, with no switch: the log opens with Android's record of that end (a native crash,
              the same signal, the same thread, the same address in libmain.so as the app's own line), keeps the
              whole report as a file, and the app runs on to three captured frames;
  abort       the same with abort(), which reaches the system by another road (the signal is sent, not raised);
  abort-after the next start again.

    py -3.13 -u crash_check.py <out dir> --apk <apk> --rom <pokemonsnap.z64> --adb <adb> [--serial <device>]
                               [--label 1.1.7-test3] [--call vkQueueSubmit] [--at 20]
"""
import argparse
import pathlib
import re
import sys
import time

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
ap.add_argument('--call', default='vkQueueSubmit')
ap.add_argument('--at', type=int, default=20)
ap.add_argument('--keep-installed', action='store_true', help='the app on the device is the one to check: install nothing')
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
if not a.keep_installed:
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


def first(lines, text):
    return next((l for l in lines if text in l), None)


def one_kind(kind, signal_name, signal_number):
    # 1. The crash itself, written by the app as it dies.
    s = ac.Stage(dev, out, kind)
    s.fresh([(a.rom, 'pokemonsnap.z64')])
    # The stage ends when the app is gone, which is the system's doing once its own report is made; twelve
    # seconds past the record's last line it is stopped by hand (a system that starts a crashed app again would
    # crash it again on the same switch), late enough not to cut the system's report short.
    s.run({'SNAP_CRASH_TEST': '%s:%s:%d' % (kind, a.call, a.at)},
          lambda lines: first(lines, '[SNAP-CRASH] the system makes its own report next') is not None, 12.0, 120)
    on = first(s.lines, '[SNAP-CRASH] the crash record is on')
    checks.add(kind, on is not None and 'for 7 signals' in on and 'loaded at 0x0' not in on, on or 'no "crash record is on" line')
    armed = first(s.lines, '[SNAP-VK] SNAP_CRASH_TEST')
    checks.add(kind, armed is not None, armed or 'the switch said nothing')
    head = first(s.lines, '[SNAP-CRASH] %s (signal %d)' % (signal_name, signal_number))
    m = re.search(r'in thread "([^"]*)" \(id (\d+)\)', head or '')
    checks.add(kind, m is not None, head or 'no "[SNAP-CRASH] %s" line; the log ends: %s' % (signal_name, ' | '.join(s.lines[-2:])))
    tid = m.group(2) if m else '?'
    call = first(s.lines, '[SNAP-CRASH] the last Vulkan call this thread began:')
    checks.add(kind, call is not None and (' %s (its call number ' % a.call) in call, call or 'no line for the thread\'s Vulkan call')
    stop = first(s.lines, '<- where it stopped')
    own = re.search(r'libmain\.so \+(0x[0-9a-f]+)', stop or '')
    frames = [l for l in s.lines if re.match(r'\[SNAP-CRASH\] #\d\d ', l)]
    inside = [l for l in frames if 'libmain.so +0x' in l]
    leads = [l for l in s.lines if l.startswith('[SNAP-CRASH] on the stack, ')]
    if kind == 'segv':
        # The write to address 0 is in libmain.so itself; abort() stops inside the C library, and this
        # emulator's libraries keep no frame records to walk back from there.
        checks.add(kind, own is not None, stop or 'no "where it stopped" line')
    checks.add(kind, len(frames) >= 1 and (kind != 'segv' or len(inside) >= 1),
               '%d addresses written, %d of them in libmain.so, and %d leads from the stack; the first: %s'
               % (len(frames), len(inside), len(leads), frames[0] if frames else '-'))
    checks.add(kind, s.met, 'the record\'s last line was written %s (the app %s)'
               % ('after %.0f s' % s.took if s.met else 'NEVER in %.0f s' % s.took, 'was gone by then' if s.ended else 'was stopped then'))
    report = s.crashed()
    checks.add(kind, bool(report), 'the system made its own report too: %s' % (report[0][:150] if report else 'NOTHING in its crash buffer'))

    # 2. The next start: Android's record of that end, in the log's first lines, and the app runs on.
    time.sleep(6.0)
    after = kind + '-after'
    s2 = ac.Stage(dev, out, after, folder=s.folder)
    s2.local.mkdir(parents=True, exist_ok=True)
    s2.run({'SNAP_PCAP_ATFRAME': '300,900,1500', 'SNAP_PCAP_BURST': '1', 'SNAP_STATS': '1'},
           lambda lines: ac.count(lines, '[SNAP-PCAP] wrote') >= 3, 3.0, 600)
    ends = [l for l in s2.lines if l.startswith('[SNAP-EXIT] ')]
    native = next((i for i, l in enumerate(ends) if 'a native crash (reason 5)' in l), None)
    checks.add(after, native is not None, ends[native] if native is not None else 'no native crash among: %s' % (' | '.join(ends[:3]) or 'no [SNAP-EXIT] line'))
    block = []
    if native is not None:
        for l in ends[native + 1:]:
            if not l.startswith('[SNAP-EXIT]   '):
                break
            block.append(l)
    sig = first(block, 'signal %d (%s)' % (signal_number, signal_name))
    checks.add(after, sig is not None, sig or 'no signal line in the record: %s' % ' | '.join(block[:2]))
    who = first(block, 'the crashing thread is')
    checks.add(after, who is not None and ('(id %s)' % tid) in who, (who or 'no thread line') + '; the app\'s own line said id %s' % tid)
    rows = [l for l in block if re.match(r'\[SNAP-EXIT\]   #\d\d pc ', l)]
    mine = [l for l in rows if 'libmain.so' in l]
    checks.add(after, len(rows) >= 3 and len(mine) >= 1, '%d frames in the record, %d of them in libmain.so' % (len(rows), len(mine)))
    if kind == 'segv' and own is not None:
        top = re.search(r'#00 pc (0x[0-9a-f]+)', rows[0] if rows else '')
        checks.add(after, top is not None and int(top.group(1), 16) == int(own.group(1), 16),
                   'where it stopped: the system says %s, the app\'s own line said libmain.so +%s' % (top.group(1) if top else '-', own.group(1)))
    kept = first(block, 'the whole report is kept as')
    size = dev.sh('stat -c %%s %s/snap64-crash.pb 2>/dev/null' % s.folder).strip()
    checks.add(after, kept is not None and size.isdigit() and int(size) > 0, '%s; the file on the device is %s bytes' % (kept or 'no "kept" line', size or 'not there'))
    version = first(s2.lines, '[SNAP] Snap64 Recomp ')
    checks.add(after, version is not None and a.label in version, version or 'no version line')
    quiet = first(s2.lines, '[SNAP-CRASH] SIG')
    checks.add(after, quiet is None, quiet or 'this start wrote no crash of its own')
    # The present's answer is not judged: this emulator says 0x3B9ACDEB (suboptimal, a turned surface) and draws.
    for text in ('[SNAP-VK] the first graphics pipeline is made', '[SNAP-VK] the first submit returned 0x0',
                 '[SNAP-VK] the first present returned', '[SNAP-VK] the first variable-count set is allocated'):
        checks.add(after, first(s2.lines, text) is not None, first(s2.lines, text) or 'no "%s" line' % text)
    means = [ac.bmp_mean(f) for f in ac.frames_of(s2)]
    lit = [m for m in means if m is not None and m > 12]
    checks.add(after, len(lit) >= 3, '%d frames with a picture (mean luminance %s)' % (len(lit), ', '.join('%.0f' % m for m in means if m is not None) or '-'))
    s2.judge_end(checks)


# The system's "has stopped" window would sit over the next start: off for the check, back after it.
dev.sh('settings put global hide_error_dialogs 1')
try:
    one_kind('segv', 'SIGSEGV', 11)
    one_kind('abort', 'SIGABRT', 6)
finally:
    dev.sh('settings delete global hide_error_dialogs')

bad = checks.failed()
print('%d checks, %d failed' % (len(checks.rows), len(bad)))
sys.exit(1 if bad else 0)
