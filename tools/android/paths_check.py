#!/usr/bin/env python3
"""The Android-only paths, driven with adb and uiautomator, with screenshots as
proof: the launcher's all-files dialog, the ROM chooser through the system's
document picker, the Back key's quit box, and "Open with" for a mod, with the
game running and from cold. tools/android_check.py covers the game; this
covers what a player does around it, by touch, as Android shows it.

    python tools/android/paths_check.py <out folder> --apk <apk> --rom <z64> --mod <snap64_zz_hooktest.nrm>
                                        [--adb <adb>] [--serial <id>]

For an emulator (or a phone that is only for testing): it uninstalls and
installs the app, so the launcher asks its first question again, and it uses
the real game folder, /sdcard/Snap64, which it moves aside to
Snap64.before-paths first and puts back at the end. A Python with Pillow is
not needed here; uiautomator's dump of the screen is read instead. The exit
code is 1 when a path failed.
"""
import argparse
import os
import pathlib
import re
import sys
import time
import xml.etree.ElementTree as ET

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import android_check as ac  # noqa: E402

sdk = pathlib.Path(os.environ.get('ANDROID_HOME', pathlib.Path.home() / 'android-sdk'))
ap = argparse.ArgumentParser(description='The Android-only paths, by touch.')
ap.add_argument('out', help='where the screenshots and logs go')
ap.add_argument('--apk', required=True)
ap.add_argument('--rom', required=True, help='pokemonsnap.z64')
ap.add_argument('--mod', required=True, help='snap64_zz_hooktest.nrm (Snap64RecompMods)')
ap.add_argument('--adb', default=str(sdk / 'platform-tools' / ('adb.exe' if os.name == 'nt' else 'adb')))
ap.add_argument('--serial')
a = ap.parse_args()
OUT = pathlib.Path(a.out)
APK, ROM, MOD = a.apk, a.rom, a.mod
OUT.mkdir(parents=True, exist_ok=True)
dev = ac.Device(a.adb, a.serial)
GAME = '/sdcard/Snap64'
results = []


def say(ok, name, text):
    (OUT / ('%s.log' % name)).write_text('\n'.join(log_lines()) + '\n', encoding='utf-8')
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


# --- 1. the launcher's own question, on a fresh install
dev.stop()
dev.run(['uninstall', ac.PKG])
note('install: %s' % dev.run(['install', APK], timeout=600).strip().splitlines()[-1])
note('all-files access before: %s' % dev.sh('appops get %s MANAGE_EXTERNAL_STORAGE' % ac.PKG).strip())
dev.sh('rm -rf %s.before-paths; if [ -d %s ]; then mv %s %s.before-paths; fi; mkdir -p %s' % (GAME, GAME, GAME, GAME, GAME))
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
dev.stop()
(OUT / 'snap64.log').write_text('\n'.join(log_lines()) + '\n', encoding='utf-8')
dev.logcat_to(OUT / 'logcat.txt', 'all')
dev.logcat_to(OUT / 'crash.txt', 'crash')
# The device as it was: the test's folder and files gone, a game folder that
# was there before put back.
dev.sh('rm -rf %s /sdcard/Download/PokemonSnap_USA.z64 /sdcard/Download/snap64_zz_hooktest.nrm; '
       'if [ -d %s.before-paths ]; then mv %s.before-paths %s; fi' % (GAME, GAME, GAME, GAME))
failed = sum(1 for r in results if not r[0])
print('\n%d paths, %d failed' % (len(results), failed))
sys.exit(1 if failed else 0)
