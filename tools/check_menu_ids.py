#!/usr/bin/env python3
"""The menu's string ids against its dynamic banks.

The pages' strings are numbered in patches/src/graphics_menu_patch.c
(`#define STR_...`), and some ranges of ids are banks the host composes
into while a page is open: the Button Setup page's row values, the Mods
page's rows, a mod's options page's window, a mod's details page's lines
and its picture. A static string whose id falls inside a bank is rewritten
the moment that bank is used, and shows the bank's text ever after (the
Controls page's Pointer row did, from 1.1.1 to 1.1.3, after a mod's
options page had been opened). This lists every static id inside a bank.

    python tools/check_menu_ids.py            # exit 1 with the list if any

The banks and their sizes are the ones graphics_menu_patch.c and
src/menu_assets.cpp agree on; a new bank goes in BANKS here too.
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATCH = os.path.join(ROOT, 'patches', 'src', 'graphics_menu_patch.c')
# name of the bank's base define: how many ids it spans
BANKS = {
    'STR_BIND_DYN': 36,     # two banks of eighteen row values
    'STR_MODS_DYN': 24,     # two banks of six names and six help lines
    'STR_OPT_DYN': 36,      # two banks of six names, six values, six help lines
    'STR_DET_LINE': 24,     # the details page's lines
    'STR_DET_THUMB': 8,     # the picture's eight bands
}


def main():
    text = io.open(PATCH, encoding='utf-8').read()
    defs = {}
    for m in re.finditer(r'^#define (STR_[A-Z0-9_]+)\s+(\d+)', text, re.M):
        defs[m.group(1)] = int(m.group(2))
    missing = [b for b in BANKS if b not in defs]
    if missing:
        print('check_menu_ids: bank defines not found: %s' % ', '.join(missing))
        return 2
    ranges = {b: (defs[b], defs[b] + n - 1) for b, n in BANKS.items()}
    bad = []
    for name in sorted(defs, key=defs.get):
        v = defs[name]
        for b, (lo, hi) in ranges.items():
            if lo <= v <= hi and name != b:
                bad.append('%s = %d is inside %s %d..%d' % (name, v, b, lo, hi))
    if bad:
        print('check_menu_ids: %d static id(s) inside a dynamic bank:' % len(bad))
        for line in bad:
            print('  ' + line)
        return 1
    print('check_menu_ids: %d ids, none inside the %d dynamic banks' % (len(defs), len(BANKS)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
