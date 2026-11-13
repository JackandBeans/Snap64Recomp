#!/usr/bin/env python3
"""The Android-only paths, driven with adb and uiautomator, with screenshots as
proof: the launcher's question about the game folder (a tap beside it, Back,
"Not now" and "Don't ask again", each followed by the next start, then the
access granted later, which copies the app's own folder into Snap64), its
all-files dialog, the ROM chooser through the system's document picker, the
Back key's quit box, and "Open with" for a mod, with the game running and
from cold. tools/android_check.py covers the game; this covers what a player
does around it, by touch, as Android shows it.

    python tools/android/paths_check.py <out folder> --apk <apk> --rom <z64> --mod <snap64_zz_hooktest.nrm>
                                        [--adb <adb>] [--serial <id>]
    python tools/android/paths_check.py <out folder> --apk <apk> --question-only

For an emulator (or a phone that is only for testing): it uninstalls and
installs the app, so the launcher asks its first question again, and it uses
the real game folder, /sdcard/Snap64, which it moves aside to
Snap64.before-paths first and puts back at the end. A Python with Pillow is
not needed here; uiautomator's dump of the screen is read instead. The exit
code is 1 when a path failed. --question-only runs the question's paths
alone, which need neither the ROM nor the mod (an older APK fails them: 1.1.6
let a tap beside the question close it for good, the Odin 3 report of
Oct 24 2026).
"""
import argparse
import os
import pathlib
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import android_check as ac  # noqa: E402

sdk = pathlib.Path(os.environ.get('ANDROID_HOME', pathlib.Path.home() / 'android-sdk'))
ap = argparse.ArgumentParser(description='The Android-only paths, by touch.')
ap.add_argument('out', help='where the screenshots and logs go')
ap.add_argument('--apk', required=True)
ap.add_argument('--rom', help='pokemonsnap.z64')
ap.add_argument('--mod', help='snap64_zz_hooktest.nrm (Snap64RecompMods)')
ap.add_argument('--question-only', action='store_true', help='the launcher\'s question alone')
ap.add_argument('--adb', default=str(sdk / 'platform-tools' / ('adb.exe' if os.name == 'nt' else 'adb')))
ap.add_argument('--serial')
a = ap.parse_args()
if not a.question_only and not (a.rom and a.mod):
    ap.error('--rom and --mod are needed unless --question-only')
OUT = pathlib.Path(a.out)
APK, ROM, MOD = a.apk, a.rom, a.mod
OUT.mkdir(parents=True, exist_ok=True)
dev = ac.Device(a.adb, a.serial)
GAME = '/sdcard/Snap64'
# The app's own folder: the game folder without all-files access, and the
# toast that names it at a start there (Snap64Launcher.java).
OWN = '/sdcard/Android/data/%s/files' % ac.PKG
OWN_TOAST = 'Game folder: the app\'s own, in Android/data'
results = []


def say(ok, name, text, lines=None):
    (OUT / ('%s.log' % name)).write_text('\n'.join(log_lines() if lines is None else lines) + '\n', encoding='utf-8')
    results.append((ok, name, text))
    print('%s  %-8s %s' % ('PASS' if ok else 'FAIL', name, text), flush=True)


def note(text):
    print('         %s' % text, flush=True)


def shot(name):
    dev.screencap(OUT / (name + '.png'))
    return OUT / (name + '.png')


def nodes():
    dev.sh('uiautomator dump /sdcard/ui.xml')
    data = dev.cat('/sdcard/ui.xml')
    if not data:
        return []
    try:
        root = ET.fromstring(data.decode('utf-8', errors='replace'))
    except ET.ParseError:
        return []
    return list(root.iter('node'))


def center(n):
    x1, y1, x2, y2 = map(int, re.findall(r'\d+', n.get('bounds')))
    return (x1 + x2) // 2, (y1 + y2) // 2


def find(pred, cap=30.0):
    t0 = time.time()
    while time.time() - t0 < cap:
        for n in nodes():
            if pred(n):
                return n
        time.sleep(1.0)
    return None


def text_is(*texts):
    want = [t.lower() for t in texts]
    return lambda n: (n.get('text') or '').strip().lower() in want


def text_has(part):
    return lambda n: part.lower() in (n.get('text') or '').lower()


def tap(n, what):
    x, y = center(n)
    dev.sh('input tap %d %d' % (x, y))
    note('tapped %s at %d,%d' % (what, x, y))


def log_lines():
    return ac.text_lines(dev.cat(GAME + '/snap64.log'))


def wait_log(pred, cap):
    t0 = time.time()
    while time.time() - t0 < cap:
        ls = log_lines()
        if pred(ls):
            return ls
        time.sleep(2.0)
    return log_lines()


def first(ls, part):
    return next((l for l in ls if part in l), None)


def finish():
    dev.stop()
    (OUT / 'snap64.log').write_text('\n'.join(log_lines()) + '\n', encoding='utf-8')
    dev.logcat_to(OUT / 'logcat.txt', 'all')
    dev.logcat_to(OUT / 'crash.txt', 'crash')
    # The device as it was: the test's folder and files gone, a game folder
    # that was there before put back.
    dev.sh('rm -rf %s /sdcard/Download/PokemonSnap_USA.z64 /sdcard/Download/snap64_zz_hooktest.nrm; '
           'if [ -d %s.before-paths ]; then mv %s.before-paths %s; fi' % (GAME, GAME, GAME, GAME))
    failed = sum(1 for r in results if not r[0])
    print('\n%d paths, %d failed' % (len(results), failed))
    sys.exit(1 if failed else 0)


def front():
    """The activity in front, as the activity manager names it."""
    lines = dev.sh('dumpsys activity activities | grep topResumedActivity').strip().splitlines()
    return lines[0].strip() if lines else ''


def own_lines():
    return ac.text_lines(dev.cat(OWN + '/snap64.log'))


def wait_own(cap):
    """The game's first line in the app's own folder, once the game has
    started there, and the folder's log."""
    t0 = time.time()
    while True:
        ls = own_lines()
        line = first(ls, '[SNAP] data directory:')
        if line is not None or time.time() - t0 >= cap:
            return line, ls
        time.sleep(1.0)


def toasts_during(action, seconds):
    """The texts of the toasts Android shows while action runs and for the
    seconds after: uiautomator's dump of the screen never holds a toast, but
    its stream of accessibility events does (TYPE_NOTIFICATION_STATE_CHANGED)."""
    cmd = [dev.adb] + (['-s', dev.serial] if dev.serial else []) + ['shell', 'timeout %d uiautomator events' % seconds]
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    time.sleep(2.0)
    action()
    out = p.communicate(timeout=seconds + 30)[0].decode('utf-8', errors='replace')
    return [m.group(1) for m in re.finditer(r'TYPE_NOTIFICATION_STATE_CHANGED;.*?Text: \[(.*?)\];', out)]


def start_asked(cap):
    """A start from the launcher's icon; the question's Allow button, or None."""
    dev.stop()
    dev.sh('am start -n %s' % ac.LAUNCHER)
    return find(text_is('Allow'), cap)


# --- the app, fresh, and the game folder moved aside
dev.stop()
dev.run(['uninstall', ac.PKG])
note('install: %s' % dev.run(['install', APK], timeout=600).strip().splitlines()[-1])
note('all-files access before: %s' % dev.sh('appops get %s MANAGE_EXTERNAL_STORAGE' % ac.PKG).strip())
dev.sh('rm -rf %s.before-paths; if [ -d %s ]; then mv %s %s.before-paths; fi; mkdir -p %s' % (GAME, GAME, GAME, GAME, GAME))


# --- 0. the launcher's question, which only an answer settles: a tap beside
# it leaves it up, Back closes the app and the next start asks again, "Not
# now" asks again at the next start, and "Don't ask again" keeps the app's own
# folder, which a toast names at every start. (1.1.6 marked the question
# asked before it showed it, and a tap beside it closed it for good: the
# game started in the app's own folder and never asked again.)
def question():
    dev.run(['logcat', '-b', 'all', '-c'])
    allow = start_asked(30)
    shot('c0-1-question')
    if allow is None:
        say(False, 'beside', 'no question at the first start', own_lines())
        return
    # A tap on the dimmed screen beside the question, as on the Odin 3:
    # above it, halfway to the screen's top (its sides are mostly the
    # window's own transparent margin).
    x1, y1, x2, y2 = map(int, re.findall(r'\d+', nodes()[0].get('bounds')))
    x, y = (x1 + x2) // 2, y1 // 2
    note('the question\'s window: %d,%d to %d,%d' % (x1, y1, x2, y2))
    dev.sh('input tap %d %d' % (x, y))
    time.sleep(3.0)
    still = find(text_is('Allow'), 3)
    shot('c0-2-beside')
    if still is None:
        line, ls = wait_own(30)
        say(False, 'beside', 'a tap at %d,%d beside the question closed it; the game started: %s' % (x, y, line), ls)
    else:
        say(True, 'beside', 'a tap at %d,%d beside the question left it up; in front: %s' % (x, y, front()))

    again = start_asked(15)
    shot('c0-3-next-start')
    if again is None:
        line, ls = wait_own(30)
        say(False, 'next', 'no question at the next start; the game started: %s' % line, ls)
        return
    say(True, 'next', 'the question was up again at the next start')

    # Back: the app closes unanswered, the game does not start, and the
    # next start asks again.
    dev.sh('rm -f %s/snap64.log' % OWN)
    dev.sh('input keyevent KEYCODE_BACK')
    time.sleep(3.0)
    shown = front()
    started = dev.exists(OWN + '/snap64.log')
    again = start_asked(15)
    shot('c0-4-after-back')
    say(ac.PKG not in shown and not started and again is not None, 'qback',
        'after Back, in front: %s; the game started: %s; the question at the next start: %s'
        % (shown or '?', started, again is not None))
    if again is None:
        return

    # "Not now": this start in the app's own folder, which a toast names,
    # and the question again at the next start.
    later = find(text_is('Not now'), 5)
    if later is None:
        say(False, 'notnow', 'the question has no "Not now" button')
        return
    texts = toasts_during(lambda: tap(later, 'Not now'), 12)
    line, ls = wait_own(60)
    shot('c0-5-not-now')
    named = any(OWN_TOAST in t for t in texts)
    again = start_asked(15)
    say(line is not None and '/Android/data/' in line and named and again is not None, 'notnow',
        'the game started: %s; the toasts: %s; the question at the next start: %s'
        % (line, texts or 'none', again is not None), ls)
    if again is None:
        return

    # "Don't ask again": the app's own folder, now and at the next start,
    # with no question and the toast again.
    never = find(text_is('Don\'t ask again'), 5)
    if never is None:
        say(False, 'never', 'the question has no "Don\'t ask again" button')
        return
    tap(never, 'Don\'t ask again')
    line, ls = wait_own(60)
    dev.stop()
    dev.sh('rm -f %s/snap64.log' % OWN)
    texts = toasts_during(lambda: dev.sh('am start -n %s' % ac.LAUNCHER), 12)
    asked = find(text_is('Allow'), 5)
    line2, ls2 = wait_own(30)
    shot('c0-6-never-next-start')
    named = any(OWN_TOAST in t for t in texts)
    say(line is not None and asked is None and line2 is not None and named, 'never',
        'the game started: %s; at the next start, the question: %s, the game: %s, the toasts: %s'
        % (line, asked is not None, line2, texts or 'none'), ls2)
    if asked is not None or line2 is None:
        return

    # The access granted later, in the system's settings: the next start is
    # in Snap64, with the app's own folder's files copied across (a save
    # stands in for them) and a toast saying so.
    dev.stop()
    made = dev.sh('mkdir -p %s/saves; echo paths > %s/saves/zz_paths_copy.txt; ls -la %s/saves'
                  % (OWN, OWN, OWN))
    # The shell writes the test file through the shared storage's FUSE layer and the app reads its own
    # folder straight from the disk below it. Started the moment after, the app twice listed its folder
    # without the file (Nov 12: "12 files copied", the passing run "13"), so it gets three seconds to
    # appear. A player's own files were written by the app itself, long before.
    time.sleep(3.0)
    dev.sh('rm -rf %s; mkdir -p %s' % (GAME, GAME))
    dev.sh('appops set %s MANAGE_EXTERNAL_STORAGE allow' % ac.PKG)
    texts = toasts_during(lambda: dev.sh('am start -n %s' % ac.LAUNCHER), 12)
    ls = wait_log(lambda ls: first(ls, '[SNAP] data directory:') is not None, 60)
    shot('c0-7-copied')
    folder = first(ls, '[SNAP] data directory:')
    public = folder is not None and ('/sdcard/Snap64/' in folder or '/storage/emulated/0/Snap64/' in folder)
    # The copy lands before the game starts, but the shell's view of the
    # folder can lag the app's (Oct 29 and Nov 12: the toast said copied
    # and the shell did not see the file in ten seconds). The folder is
    # listed, not looked up by name, for half a minute, and the launcher's
    # own lines (tag Snap64, left out of logcat.txt with the game's) are
    # kept: its count of the files it copied, and any it could not.
    copied = False
    t_copy = time.time()
    for _ in range(30):
        listing = dev.sh('ls %s/saves 2>/dev/null' % GAME)
        copied = 'zz_paths_copy.txt' in listing.split()
        if copied:
            break
        time.sleep(1.0)
    waited = time.time() - t_copy
    launcher = [l for l in dev.run(['logcat', '-d', '-s', 'Snap64:V']).splitlines()
                if ' copied from ' in l or 'could not' in l]
    (OUT / 'copy-launcher.txt').write_text('\n'.join(launcher) + '\n', encoding='utf-8')
    # Both sides of the copy as the shell sees them (run-as is refused on the shared storage).
    (OUT / 'copy-listing.txt').write_text(
        '--- the test file as the shell made it (%s/saves):\n%s\n'
        '--- Snap64 after the copy:\n%s\n--- the app\'s own folder after the copy:\n%s\n'
        % (OWN, made, dev.sh('ls -laR %s 2>&1 | head -80' % GAME), dev.sh('ls -laR %s 2>&1 | head -80' % OWN)),
        encoding='utf-8')
    said = any('copied to Internal storage > Snap64' in t for t in texts)
    say(public and copied and said, 'copy', '%s; the save in Snap64: %s (the shell saw it after %.0f s); '
        'the launcher: %s; the toasts: %s'
        % (folder or 'no log in /sdcard/Snap64', copied, waited,
           ' | '.join(l.split(': ', 1)[-1] for l in launcher[-3:]) or 'no line', texts or 'none'))
    dev.stop()
    dev.sh('appops set %s MANAGE_EXTERNAL_STORAGE default' % ac.PKG)


question()
if a.question_only:
    finish()

# --- 1. the launcher's own question, from an unanswered start ("Don't ask
# again" settled it above; the app's data cleared unsettles it)
dev.stop()
dev.sh('pm clear %s' % ac.PKG)
dev.sh('rm -rf %s %s; mkdir -p %s' % (OWN, GAME, GAME))
dev.push(ROM, GAME + '/pokemonsnap.z64')
dev.run(['logcat', '-b', 'all', '-c'])
dev.sh('am start -n %s' % ac.LAUNCHER)
allow = find(text_is('Allow'), 30)
shot('c1-1-dialog')
if allow is None:
    say(False, 'access', 'the launcher\'s dialog never showed an Allow button')
else:
    msg = find(text_has('Internal storage > Snap64'), 5)
    note('dialog text: %s' % ((msg.get('text') or '')[:160].replace('\n', ' ') if msg is not None else '(not read)'))
    tap(allow, 'Allow')
    row = find(lambda n: n.get('class') == 'android.widget.Switch' or 'manage all files' in (n.get('text') or '').lower(), 30)
    shot('c1-2-settings')
    if row is None:
        say(False, 'access', 'the system\'s all-files page did not show its switch')
    else:
        switch = find(lambda n: n.get('class') == 'android.widget.Switch', 5) or row
        note('the page\'s switch before: checked=%s' % switch.get('checked'))
        tap(switch, 'the all-files switch')
        time.sleep(2.0)
        switch2 = find(lambda n: n.get('class') == 'android.widget.Switch', 10)
        shot('c1-3-settings-on')
        note('the page\'s switch after: checked=%s' % (switch2.get('checked') if switch2 is not None else '?'))
        dev.sh('input keyevent KEYCODE_BACK')
        note('pressed Back to leave the settings page')
        ls = wait_log(lambda ls: any('menu font harvested' in l for l in ls), 120)
        shot('c1-4-game')
        folder = first(ls, '[SNAP] data directory:')
        public = folder is not None and ('/sdcard/Snap64/' in folder or '/storage/emulated/0/Snap64/' in folder)
        ok = public and any('menu font harvested' in l for l in ls)
        say(ok, 'access', '%s; appops now: %s' % (folder or 'no log in /sdcard/Snap64',
                                                  dev.sh('appops get %s MANAGE_EXTERNAL_STORAGE' % ac.PKG).strip()))

# --- 2. the ROM chooser: no ROM in the folder, the system's document picker
dev.stop()
time.sleep(1.0)
dev.sh('rm -f %s/pokemonsnap.z64 %s/snap64.running' % (GAME, GAME))
dev.push(ROM, '/sdcard/Download/PokemonSnap_USA.z64')
dev.sh('content call --method scan_volume --uri content://media --arg external_primary')
dev.sh('am start -n %s' % ac.LAUNCHER)
choose = find(text_is('Choose the file'), 90)
shot('c2-1-ask')
if choose is None:
    say(False, 'rompick', 'the question box never showed "Choose the file"; the log: %s' % ' | '.join(log_lines()[-3:]))
else:
    tap(choose, 'Choose the file')
    time.sleep(3.0)
    shot('c2-2-picker')
    target = find(text_is('PokemonSnap_USA.z64'), 8)
    if target is None:
        # The Downloads view lists what the media store knows, and a file
        # pushed by adb is not in it: the device's own storage, then its
        # Download folder, as a player browsing by folder would.
        model = dev.sh('getprop ro.product.model').strip()
        roots = find(lambda n: (n.get('content-desc') or '') == 'Show roots', 8)
        if roots is not None:
            tap(roots, 'Show roots')
            storage = find(text_is(model), 8)
            if storage is not None:
                tap(storage, 'the device\'s storage (%s)' % model)
                folder = find(text_is('Download'), 10)
                if folder is not None:
                    tap(folder, 'the Download folder')
        target = find(text_is('PokemonSnap_USA.z64'), 15)
    shot('c2-3-picker-file')
    if target is None:
        say(False, 'rompick', 'uiautomator found no PokemonSnap_USA.z64 in the document picker')
    else:
        tap(target, 'PokemonSnap_USA.z64')
        ls = wait_log(lambda ls: any('menu font harvested' in l for l in ls), 180)
        shot('c2-4-game')
        copied = next((l for l in ls if l.startswith('[SNAP] ROM:') and 'copied to' in l), None)
        has = dev.exists(GAME + '/pokemonsnap.z64')
        say(copied is not None and has and any('menu font harvested' in l for l in ls), 'rompick',
            '%s; the folder holds pokemonsnap.z64: %s' % (copied or 'no "copied to" line', has))

# --- 3. Back during the title: the quit box, closed again
ls = log_lines()
if any('menu font harvested' in l for l in ls) and dev.alive():
    time.sleep(5.0)
    dev.sh('input keyevent KEYCODE_BACK')
    keep = find(text_is('Keep playing'), 20)
    shot('c3-1-quit-box')
    if keep is None:
        say(False, 'back', 'no box with "Keep playing" after Back; the log: %s' % ' | '.join(log_lines()[-3:]))
    else:
        quit_btn = find(text_is('Quit'), 3)
        note('the box\'s buttons: Keep playing%s' % (', Quit' if quit_btn is not None else ''))
        tap(keep, 'Keep playing')
        ls = wait_log(lambda ls: any('quit declined' in l for l in ls), 20)
        time.sleep(3.0)
        shot('c3-2-after')
        declined = first(ls, '[SNAP] quit declined')
        say(declined is not None and dev.alive(), 'back', '%s; the app is %s' % (declined or 'no "quit declined" line',
                                                                                  'running' if dev.alive() else 'GONE'))
else:
    say(False, 'back', 'not tried: the game was not at the title')


# --- 4. "Open with" for a mod: with the game running, then with it closed
def open_with(tag, cold):
    dev.sh('rm -f %s/mods/snap64_zz_hooktest.nrm' % GAME)
    if cold:
        dev.stop()
        time.sleep(1.0)
        # A force-stop leaves the session mark, which would ask about mods.
        dev.sh('rm -f %s/snap64.running' % GAME)
        dev.sh('rm -f %s/snap64.log' % GAME)
    before = 0 if cold else len(log_lines())
    dev.sh('am start -a android.intent.action.VIEW -d file:///sdcard/Download/snap64_zz_hooktest.nrm '
           '-t application/octet-stream -n %s' % ac.LAUNCHER)
    wait_log(lambda ls: any('[SNAP-MODS]' in l and 'install' in l.lower() for l in ls[before:]), 120)
    time.sleep(3.0)
    shot('c4-%s-1-after-open' % tag)
    box = find(lambda n: n.get('class') == 'android.widget.Button', 10)
    if box is not None:
        note('a box over the game: "%s", button "%s"' % (
            next((n.get('text') for n in nodes() if (n.get('text') or '').strip() and n.get('class') == 'android.widget.TextView'), '?')[:200].replace('\n', ' '),
            box.get('text')))
        tap(box, 'the box\'s button "%s"' % box.get('text'))
        time.sleep(2.0)
        shot('c4-%s-2-after-box' % tag)
    mods = [l for l in log_lines()[before:] if '[SNAP-MODS]' in l]
    has = dev.exists(GAME + '/mods/snap64_zz_hooktest.nrm')
    say(bool(mods) and has, 'open-' + tag, '%s; mods/snap64_zz_hooktest.nrm in the folder: %s; the app is %s'
        % (' | '.join(mods[:3]) or 'no [SNAP-MODS] line', has, 'running' if dev.alive() else 'GONE'))


dev.push(MOD, '/sdcard/Download/snap64_zz_hooktest.nrm')
if not (dev.alive() and any('menu font harvested' in l for l in log_lines())):
    note('the game was not at the title; started for the running case')
    dev.stop()
    dev.sh('rm -f %s/snap64.running' % GAME)
    dev.sh('am start -n %s' % ac.LAUNCHER)
    wait_log(lambda ls: any('menu font harvested' in l for l in ls), 120)
open_with('run', False)
open_with('cold', True)
finish()
