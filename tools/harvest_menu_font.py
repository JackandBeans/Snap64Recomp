#!/usr/bin/env python3
"""Harvest the menu sprite font: the actual letterforms of the Options
screen, cut from the original pre-rendered sprites.

The menus' text is NOT drawn from either ROM font atlas -- the sprites
carry their own ~9px letterforms (pure white + alpha, no baked shadow).
This walks every transcribed text sprite in the main menu's VPK0 segment,
segments each line into per-character cells, verifies the segmentation
against the transcript, measures real advances, and emits the glyph set.

Run under WSL from the repo root:
  python3 tools/harvest_menu_font.py <rom> [--emit]
"""
import os
import sys

# The decomp checkout: SNAP_DECOMP if set, else ~/pokemonsnap (BUILDING.md).
sys.path.insert(0, os.path.join(os.path.expanduser(os.environ.get("SNAP_DECOMP", "~/pokemonsnap")), "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vpk0_codec import decompress_vpk0
from extract_menu_dot import decode_sprite, ROM_VPK0

CELL_H = 10
CORE = 128

# (vram, text[, 'white']) -- '|' separates lines within one sprite; '<',
# '>', '@' (the bullet dot) mark runs to skip. 'white' = the sprite has a
# dark backing plate; segment and keep only white-core pixels. Ordered so
# the Options screen's own strips take priority for duplicated characters.
SOURCES = [
    (0x8033F498, "@Screen"),
    (0x8033FBB8, "@Sound"),
    (0x8033E938, "@Z Button Setup"),
    (0x8033E210, "@Control Stick Setup"),
    (0x8033ECD0, "@Return"),
    (0x80342FF0, "<Stereo>"),
    (0x80342150, "<Mono>"),
    (0x80341C70, "<Hold>"),
    (0x803434D0, "<Switch>"),
    (0x80342B10, "<Reverse>"),
    (0x80342630, "<Normal>"),
    (0x8032F360, "Display setting on screen."),
    (0x80339F50, "Sound setting."),
    (0x80336600, "Set sound to Mono."),
    (0x8033D8A0, "Set sound to Stereo."),
    (0x803280C0, "Z Button setting."),
    (0x80320E20, "Hold Z Button to focus."),
    (0x80324770, "Press Z Button to focus, and press|Z Button again to release."),
    (0x80316230, "Control Stick setting."),
    (0x80319B80, "Press up on Control Stick to raise view.|Press down on Control Stick to lower view."),
    (0x8031D4D0, "Press up on Control Stick to lower view.|Press down on Control Stick to raise view."),
    (0x8032BA10, "Return to title screen."),
    (0x80332CB0, "Adjust orange frame by moving Control Stick."),
    # The title-menu items ("Options", "Gallery", ...) use a LARGER variant
    # of this font -- harvesting them pollutes the set, so the two capitals
    # no small-font sprite contains (O, G) are drawn below instead.
]

# The thirteen glyphs no menu sprite contains, drawn in the sprite font's
# own style (2px stems, one antialiased fringe, caps on rows 1..9).
# '#' = solid white, '%' = strong fringe, '+' = soft fringe.
SYNTH = {
    "E": [
        ".......",
        "+#####.",
        ".##+++.",
        ".#+....",
        ".####+.",
        ".##+++.",
        ".#+....",
        ".#+..+.",
        ".#####.",
        ".+###+.",
    ],
    "V": [
        ".......",
        "##...##",
        "#+...#+",
        "#+...#+",
        "#+...#+",
        "+#..+#.",
        ".#+.#+.",
        ".##+#+.",
        ".+##+..",
        "..##...",
    ],
    "0": [
        ".....",
        "+###+",
        "##+##",
        "#+.#+",
        "#+.#+",
        "#+.#+",
        "#+.#+",
        "#+.#+",
        "####+",
        "+###+",
    ],
    "9": [
        ".....",
        "+###+",
        "##+##",
        "#+.#+",
        "#+.#+",
        "+####",
        "..+##",
        "..+#+",
        ".+##+",
        "+##+.",
    ],
    "O": [
        ".......",
        ".......",
        ".+###+.",
        ".##+##.",
        "+#%.##.",
        "+#..+#.",
        "+#..+#.",
        ".#+.+#.",
        ".##+##.",
        ".+###+.",
    ],
    "G": [
        ".......",
        ".......",
        ".+###+.",
        ".##+#%.",
        "+#%.%+.",
        "+#.....",
        "+#.+##.",
        ".#+.+#.",
        ".##+##.",
        ".%###+.",
    ],
    "W": [
        ".......",
        "#+...+#",
        "#+...+#",
        "#+...+#",
        "#+.+.+#",
        "#++#++#",
        "#+###+#",
        "###+###",
        "##+.+##",
        "#+...+#",
    ],
    "T": [
        "......",
        "######",
        "##+###",
        "+.##.+",
        "..##..",
        "..#+..",
        "..#+..",
        "..#+..",
        "..##..",
        "..##..",
    ],
    "F": [
        ".......",
        "+#####.",
        ".##+++.",
        ".#+....",
        ".####+.",
        ".##+++.",
        ".#+....",
        ".#+....",
        ".#+....",
        ".##....",
    ],
    "L": [
        ".......",
        ".##....",
        ".#+....",
        ".#+....",
        ".#+....",
        ".#+....",
        ".#+....",
        ".#+..+.",
        ".#####.",
        ".+###+.",
    ],
    "1": [
        ".....",
        ".+#+.",
        "+##+.",
        "#+#+.",
        "..#+.",
        "..#+.",
        "..#+.",
        "..#+.",
        ".###+",
        ".+#+.",
    ],
    "2": [
        ".....",
        "+###+",
        "##+##",
        "+..#+",
        "..+#+",
        ".+#+.",
        ".##..",
        "+#+..",
        "#####",
        "+###+",
    ],
    "3": [
        ".....",
        "+###+",
        "##+##",
        "...#+",
        ".+##+",
        ".+###",
        "...##",
        "+..#+",
        "#####",
        "+###+",
    ],
    "4": [
        ".....",
        "..+#+",
        ".+###",
        ".##.#",
        "+#+.#",
        "##..#",
        "#####",
        "+++##",
        "...#+",
        "...#+",
    ],
    "5": [
        ".....",
        "#####",
        "#+++.",
        "#+...",
        "####+",
        "+++##",
        "...#+",
        "+..##",
        "####+",
        "+##+.",
    ],
    "6": [
        ".....",
        ".+##+",
        ".##+#",
        "+#+..",
        "####+",
        "##+##",
        "#+.#+",
        "#+.#+",
        "####+",
        "+##+.",
    ],
    "7": [
        ".....",
        "#####",
        "#++##",
        "..+#+",
        "..##.",
        ".+#+.",
        ".##..",
        ".#+..",
        ".#+..",
        ".#+..",
    ],
    "8": [
        ".....",
        "+###+",
        "##+##",
        "#+.#+",
        "+###+",
        "##+##",
        "#+.#+",
        "#+.#+",
        "####+",
        "+###+",
    ],
    "x": [
        ".....",
        ".....",
        ".....",
        ".....",
        "##.##",
        "+#+#+",
        ".+#+.",
        ".###.",
        "##+##",
        "#+.+#",
    ],
    "z": [
        ".....",
        ".....",
        ".....",
        ".....",
        "####+",
        "++##+",
        ".+#+.",
        ".##..",
        "#####",
        "+###+",
    ],
    "-": [
        ".....",
        ".....",
        ".....",
        ".....",
        ".....",
        "####+",
        "+++#.",
        ".....",
        ".....",
        ".....",
    ],
}


def synth_cell(art):
    level = {"#": 255, "%": 160, "+": 80, ".": 0}
    w = len(art[0])
    cell = [[(255, level[ch]) for ch in row] for row in art]
    return (w, 0, w, cell)


# ---------------------------------------------------------------------------
# The help face: the antialiased rendering of the menu letterforms used by
# the twelve stock help sentences (all 238x30 IA16; intensity is 255 on
# every inked texel and ALL of the antialiasing lives in alpha, unlike the
# hard-edged label/value sprites the body face is cut from). Harvested so
# the port's own help and description strings sit in the box with the same
# soft edges as the stock sentences around them. Two-line sprites carry
# their second line exactly 12 rows below the first, which is also the
# pitch compose_lines stacks at. The canonical sentence goes first so its
# cells win the first-occurrence contest.
HLP_SOURCES = [
    (0x8032F360, "Display setting on screen."),
    (0x80316230, "Control Stick setting."),
    (0x80319B80, "Press up on Control Stick to raise view.|Press down on Control Stick to lower view."),
    (0x8031D4D0, "Press up on Control Stick to lower view.|Press down on Control Stick to raise view."),
    (0x80320E20, "Hold Z Button to focus."),
    (0x80324770, "Press Z Button to focus, and press|Z Button again to release."),
    (0x803280C0, "Z Button setting."),
    (0x8032BA10, "Return to title screen."),
    (0x80332CB0, "Adjust orange frame by moving Control Stick."),
    (0x80336600, "Set sound to Mono."),
    (0x80339F50, "Sound setting."),
    (0x8033D8A0, "Set sound to Stereo."),
]
HLP_CELL_H = 12

# Characters the twelve sentences never use, drawn in the help face's own
# proportions (9-row caps on cell rows 1-9, x-height rows 4-9, descenders
# below) with hand-placed antialiasing: '#'=255, '%'=160, '+'=80.
HLP_SYNTH = {
    "L": [
        "....",
        "#...",
        "#...",
        "#...",
        "#...",
        "#...",
        "#...",
        "#...",
        "#..+",
        "####",
        "....",
        "....",
    ],
    "N": [
        ".....",
        "#...#",
        "##..#",
        "##+.#",
        "#.#.#",
        "#.#.#",
        "#.+##",
        "#..##",
        "#...#",
        "#...#",
        ".....",
        ".....",
    ],
    "O": [
        ".+++.",
        "+###+",
        "#+.+#",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        "#+.+#",
        "+###+",
        ".....",
        ".....",
    ],
    "T": [
        ".....",
        "#####",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        ".....",
        ".....",
    ],
    "W": [
        ".....",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        "#.+.#",
        "#.#.#",
        "#.#.#",
        "#%#%#",
        "+#.#+",
        ".....",
        ".....",
    ],
    "h": [
        "...",
        "#..",
        "#..",
        "#..",
        "#%#",
        "#.#",
        "#.#",
        "#.#",
        "#.#",
        "#.#",
        "...",
        "...",
    ],
    "x": [
        "...",
        "...",
        "...",
        "...",
        "#.#",
        "#+#",
        ".#.",
        ".#.",
        "#+#",
        "#.#",
        "...",
        "...",
    ],
    "z": [
        "....",
        "....",
        "....",
        "....",
        "####",
        "...#",
        "..#+",
        ".#+.",
        "#+..",
        "####",
        "....",
        "....",
    ],
    "2": [
        "....",
        "+##+",
        "#..#",
        "...#",
        "...#",
        "..+#",
        ".+#+",
        "+#+.",
        "#...",
        "####",
        "....",
        "....",
    ],
    "3": [
        "....",
        "+##+",
        "#..#",
        "...#",
        ".+#+",
        "...#",
        "...#",
        "...#",
        "#..#",
        "+##+",
        "....",
        "....",
    ],
    "4": [
        "....",
        "..+#",
        ".+##",
        "+#.#",
        "#..#",
        "####",
        "...#",
        "...#",
        "...#",
        "...#",
        "....",
        "....",
    ],
    "6": [
        "....",
        "+##+",
        "#..+",
        "#...",
        "###+",
        "#..#",
        "#..#",
        "#..#",
        "#..#",
        "+##+",
        "....",
        "....",
    ],
}

# ---------------------------------------------------------------------------
# The credits face: the 1px-stroke condensed font of the title screen's
# copyright lines (sprite 0x802F82C8). Harvested for the port's own third
# line; the characters the copyright text lacks are drawn in its style.
# No commas in the transcript: their tails tuck under the neighbouring
# digits' columns and never form separate runs. Digit cells are clipped
# below their baseline so a swallowed comma tail cannot ride along.
CRD_SOURCE = (0x802F82C8, "@1995 1996 1998 Nintendo/Creatures/GAMEFREAK")
CRD_SYNTH = {
    "J": [
        ".....",
        "..###",
        "...#.",
        "...#.",
        "...#.",
        "...#.",
        "#..#.",
        ".##..",
        ".....",
        ".....",
    ],
    "B": [
        ".....",
        "###..",
        "#..#.",
        "#..#.",
        "###..",
        "#..#.",
        "#..#.",
        "###..",
        ".....",
        ".....",
    ],
    "S": [
        ".....",
        ".###.",
        "#....",
        "#....",
        ".##..",
        "...#.",
        "...#.",
        "###..",
        ".....",
        ".....",
    ],
    "k": [
        ".....",
        "#....",
        "#....",
        "#..#.",
        "#.#..",
        "##...",
        "#.#..",
        "#..#.",
        ".....",
        ".....",
    ],
    "m": [
        ".....",
        ".....",
        ".....",
        "####.",
        "#.#.#",
        "#.#.#",
        "#.#.#",
        "#.#.#",
        ".....",
        ".....",
    ],
    "p": [
        ".....",
        ".....",
        ".....",
        "###..",
        "#..#.",
        "#..#.",
        "###..",
        "#....",
        "#....",
        ".....",
    ],
    "c": [
        ".....",
        ".....",
        ".....",
        ".###.",
        "#....",
        "#....",
        "#....",
        ".###.",
        ".....",
        ".....",
    ],
    "v": [
        ".....",
        ".....",
        ".....",
        "#..#.",
        "#..#.",
        "#..#.",
        ".##..",
        ".##..",
        ".....",
        ".....",
    ],
    "&": [
        ".....",
        ".#...",
        "#.#..",
        "#.#..",
        ".#...",
        "#.#.#",
        "#..#.",
        ".##.#",
        ".....",
        ".....",
    ],
    "(": [
        ".....",
        "..#..",
        ".#...",
        ".#...",
        ".#...",
        ".#...",
        ".#...",
        "..#..",
        ".....",
        ".....",
    ],
    ")": [
        ".....",
        "..#..",
        "...#.",
        "...#.",
        "...#.",
        "...#.",
        "...#.",
        "..#..",
        ".....",
        ".....",
    ],
    "0": [
        ".....",
        ".##..",
        "#..#.",
        "#..#.",
        "#..#.",
        "#..#.",
        "#..#.",
        ".##..",
        ".....",
        ".....",
    ],
    "4": [
        ".....",
        "..##.",
        ".#.#.",
        "#..#.",
        "####.",
        "...#.",
        "...#.",
        "...#.",
        ".....",
        ".....",
    ],
    ".": [
        ".....",
        ".....",
        ".....",
        ".....",
        ".....",
        ".....",
        ".....",
        "#....",
        ".....",
        ".....",
    ],
    "\x01": [
        ".....",
        ".....",
        ".....",
        ".....",
        "##...",
        "##...",
        ".....",
        ".....",
        ".....",
        ".....",
    ],
}

# ---------------------------------------------------------------------------
# The header font: the medium ~10px face of the "Options" screen title
# (sprite 0x80341790). Only that one sprite exists in this face, so the
# letters "Graphics" needs beyond it are drawn here in its style: 2px
# strokes, squarish bowls, minimal antialiasing.
HDR_CELL_H = 12
HDR_SOURCE = (0x80341790, "Options")
HDR_SYNTH = {
    "S": [
        "........",
        ".+####+.",
        "+######.",
        "##+..+#.",
        "##+.....",
        "+####+..",
        "..+####+",
        ".....+##",
        "#+...+##",
        ".######+",
        ".+####+.",
        "........",
    ],
    "u": [
        ".......",
        ".......",
        ".......",
        ".......",
        "#+...##",
        "#+...##",
        "#+...##",
        "#+...##",
        "##...##",
        "##+.+##",
        ".#####+",
        ".......",
    ],
    "d": [
        ".......",
        "....+##",
        "....+##",
        "....+##",
        ".+##+##",
        "+#+.+##",
        "##...##",
        "##...##",
        "##..+##",
        "##+.+##",
        ".#####+",
        ".......",
    ],
    "G": [
        "........",
        ".+####+.",
        ".######+",
        "##+..+##",
        "##......",
        "#+......",
        "#+..+###",
        "##....##",
        "##...+##",
        ".######+",
        ".+####+.",
        "........",
    ],
    "r": [
        ".......",
        ".......",
        ".......",
        ".......",
        "#+.###.",
        "#+####+",
        "###+.#+",
        "##+....",
        "#+.....",
        "#+.....",
        "##.....",
        ".......",
    ],
    "a": [
        ".......",
        ".......",
        ".......",
        ".......",
        ".+####+",
        ".#++.##",
        "....+##",
        ".+#####",
        "##+..##",
        "##..+##",
        ".#####+",
        ".......",
    ],
    "c": [
        ".......",
        ".......",
        ".......",
        ".......",
        ".+####.",
        ".##++#+",
        "##+....",
        "#+.....",
        "##.....",
        ".##++#.",
        ".+####.",
        ".......",
    ],
    "h": [
        ".......",
        ".##....",
        ".#+....",
        ".#+....",
        ".#+###.",
        ".######",
        ".##+.##",
        ".#+..##",
        ".#+..##",
        ".#+..##",
        ".##..##",
        ".......",
    ],
}


def make_pred(white):
    if white:
        return lambda px: px[1] >= CORE and px[0] >= 160
    return lambda px: px[1] >= CORE


def find_lines(img, w, h, pred):
    """Rows split into ink bands separated by fully blank rows."""
    inked = [any(pred(img[y][x]) for x in range(w)) for y in range(h)]
    bands = []
    y = 0
    while y < h:
        if inked[y]:
            y0 = y
            while y < h and inked[y]:
                y += 1
            bands.append((y0, y))
        else:
            y += 1
    return bands


def runs_in(img, w, y0, y1, pred):
    occ = [any(pred(img[y][x]) for y in range(y0, y1)) for x in range(w)]
    out = []
    x = 0
    while x < w:
        if occ[x]:
            s = x
            while x < w and occ[x]:
                x += 1
            out.append((s, x))
        else:
            x += 1
    return out


def main():
    rom_path = sys.argv[1]
    emit = "--emit" in sys.argv
    with open(rom_path, "rb") as f:
        f.seek(ROM_VPK0)
        seg, _ = decompress_vpk0(f.read(0x4D416))

    glyphs = {}       # char -> (cellW, coreStart, coreEnd, rows[(i,a)])
    gaps = []         # next core start - prev core end, adjacent chars
    space_advances = []

    for entry in SOURCES:
        vram, text = entry[0], entry[1]
        white = len(entry) > 2
        pred = make_pred(white)
        img, w, h = decode_sprite(seg, vram)
        lines = text.split("|")
        bands = find_lines(img, w, h, pred)
        if len(bands) != len(lines):
            print(f"{vram:08X}: {len(bands)} bands vs {len(lines)} lines -- SKIP")
            continue
        for (y0, y1), line in zip(bands, lines):
            expect = [c for c in line if c != " "]
            rr = runs_in(img, w, y0, y1, pred)
            if len(rr) != len(expect):
                print(f"{vram:08X} '{line}': {len(rr)} runs vs {len(expect)} chars -- SKIP")
                continue
            top = y0
            for i, c in enumerate(expect):
                s, e = rr[i]
                if (c in "<>@") or (c in glyphs):
                    continue
                x0 = max(0, s - 1)
                x1 = min(w, e + 1)
                cell = []
                for y in range(top - 1, top - 1 + CELL_H):
                    row = []
                    for x in range(x0, x1):
                        px = img[y][x] if (0 <= y < h) else (0, 0)
                        if white and px[0] < 128:
                            px = (0, 0)
                        row.append(px)
                    cell.append(row)
                glyphs[c] = (x1 - x0, s - x0, e - x0, cell)
            # spacing stats: walk the line against the runs
            ri = 0
            prev_end = None
            prev_was = None
            for c in line:
                if c == " ":
                    prev_was = "space"
                    continue
                s, e = rr[ri]
                if prev_end is not None:
                    if prev_was == "space":
                        space_advances.append(s - prev_end)
                    else:
                        gaps.append(s - prev_end)
                prev_end = e
                prev_was = "char"
                ri += 1

    for c, art in SYNTH.items():
        if c not in glyphs:
            glyphs[c] = synth_cell(art)

    # Header face: harvested "Options" plus the drawn letters.
    hdr = {}
    hvram, htext = HDR_SOURCE
    himg, hw, hh = decode_sprite(seg, hvram)
    pred = make_pred(False)
    hbands = find_lines(himg, hw, hh, pred)
    hruns = runs_in(himg, hw, hbands[0][0], hbands[0][1], pred)
    if len(hruns) == len(htext):
        htop = hbands[0][0]
        for i, c in enumerate(htext):
            s, e = hruns[i]
            x0, x1 = max(0, s - 1), min(hw, e + 1)
            cell = []
            for y in range(htop - 1, htop - 1 + HDR_CELL_H):
                cell.append([himg[y][x] if 0 <= y < hh else (0, 0) for x in range(x0, x1)])
            hdr[c] = (x1 - x0, s - x0, e - x0, cell)
        print("header harvested:", "".join(sorted(hdr.keys())),
              f"(top row {htop}, cell starts {htop - 1})")
    else:
        print(f"header: {len(hruns)} runs vs {len(htext)} chars -- SKIP")
    for c, art in HDR_SYNTH.items():
        if c not in hdr:
            w = len(art[0])
            level = {"#": 255, "%": 160, "+": 80, ".": 0}
            hdr[c] = (w, 0, w, [[(255, level[ch]) for ch in row] for row in art])

    # Credits face: harvested copyright glyphs plus the drawn letters.
    crd = {}
    cvram, ctext = CRD_SOURCE
    cimg, cw, chh = decode_sprite(seg, cvram)
    cpred = make_pred(False)
    cbands = find_lines(cimg, cw, chh, cpred)
    cruns = runs_in(cimg, cw, cbands[0][0], cbands[0][1], cpred)
    cexpect = [c for c in ctext if c != " "]
    if len(cruns) == len(cexpect):
        ctop = cbands[0][0]
        for i, c in enumerate(cexpect):
            s, e = cruns[i]
            if c in crd:
                continue
            x0, x1 = max(0, s - 1), min(cw, e + 1)
            cell = []
            for row_i, y in enumerate(range(ctop - 1, ctop - 1 + CELL_H)):
                if c.isdigit() and row_i > 7:
                    cell.append([(0, 0)] * (x1 - x0))
                else:
                    cell.append([cimg[y][x] if 0 <= y < chh else (0, 0) for x in range(x0, x1)])
            crd[c] = (x1 - x0, s - x0, e - x0, cell)
        print("credits harvested:", "".join(sorted(crd.keys())))
    else:
        print(f"credits: {len(cruns)} runs vs {len(cexpect)} chars -- SKIP")
    for c, art in CRD_SYNTH.items():
        if c not in crd:
            # Trim to ink on BOTH sides so the advance matches the drawn
            # width -- a leading blank column is invisible padding that
            # shifts the glyph inside its own advance (the parens sat
            # lopsided against their neighbours for exactly this reason).
            cols = [x for row in art for x, ch in enumerate(row) if ch == "#"]
            x0 = min(cols, default=0)
            x1 = max(cols, default=0) + 1
            crd[c] = (x1 - x0, 0, x1 - x0,
                      [[(255, 255 if ch == "#" else 0) for ch in row[x0:x1]] for row in art])

    # Help face: the antialiased help sentences, cut with their alpha
    # fringe intact. Segmentation at the core threshold gives exactly one
    # run per character on every sprite; the cell then takes one extra
    # column each side so the halo travels with the glyph. The sentences
    # were laid out by a real text renderer with per-pair kerning -- the
    # gap between two letters depends on the pair ("ay" snugs to one
    # pixel where "sp" takes three) -- so every observed pair's gap is
    # collected alongside the glyphs and the composer replays it.
    hlp = {}
    hlp_kern = {}        # (prev, next) -> Counter of intra-word gaps
    hlp_space_kern = {}  # (prev, next) -> Counter of across-a-space gaps
    hlp_lines_seen = []  # (line, [(prev, cur, spaced, gap)]) for the self-test
    hpred = make_pred(False)
    for vram, text in HLP_SOURCES:
        himg2, hw2, hh2 = decode_sprite(seg, vram)
        hbands = find_lines(himg2, hw2, hh2, hpred)
        hlines = text.split("|")
        if len(hbands) != len(hlines):
            print(f"help {vram:08X}: {len(hbands)} bands vs {len(hlines)} lines -- SKIP")
            continue
        for (y0, y1), line in zip(hbands, hlines):
            expect = [c for c in line if c != " "]
            rr = runs_in(himg2, hw2, y0, y1, hpred)
            if len(rr) != len(expect):
                print(f"help {vram:08X} '{line}': {len(rr)} runs vs {len(expect)} chars -- SKIP")
                continue
            for i, c in enumerate(expect):
                if c in hlp:
                    continue
                s, e = rr[i]
                x0, x1 = max(0, s - 1), min(hw2, e + 1)
                cell = []
                for y in range(y0 - 1, y0 - 1 + HLP_CELL_H):
                    cell.append([himg2[y][x] if 0 <= y < hh2 else (0, 0) for x in range(x0, x1)])
                hlp[c] = (x1 - x0, s - x0, e - x0, cell)
            # Kerning: walk the line against its runs, one gap per pair.
            ri = 0
            prev_c = None
            prev_end = None
            spaced = False
            pairs_seen = []
            for c in line:
                if c == " ":
                    spaced = True
                    continue
                s, e = rr[ri]
                if prev_c is not None:
                    gap = s - prev_end
                    # First occurrence wins, and the canonical box sentence
                    # is harvested first: the renderer spaced some pairs
                    # differently in different sentences, and the value
                    # that matters is the one on screen NEXT to the port's
                    # own strings.
                    table = hlp_space_kern if spaced else hlp_kern
                    table.setdefault((prev_c, c), gap)
                    pairs_seen.append((prev_c, c, spaced, gap))
                prev_c = c
                prev_end = e
                spaced = False
                ri += 1
            hlp_lines_seen.append((line, pairs_seen))
    print("help harvested:", "".join(sorted(hlp.keys())))
    print(f"help kerning: {len(hlp_kern)} letter pairs, {len(hlp_space_kern)} space pairs")
    # Self-test: replaying every stock sentence through the reduced tables
    # should land each pair on the sprite's own gap. A pair the renderer
    # itself spaced inconsistently shows up here as drift.
    worst = (0, "")
    total_drift = 0
    for line, pairs_seen in hlp_lines_seen:
        drift = sum(abs((hlp_space_kern if spaced else hlp_kern)[(a, b)] - gap)
                    for (a, b, spaced, gap) in pairs_seen)
        total_drift += drift
        if drift > worst[0]:
            worst = (drift, line)
    print(f"help kerning self-test: total drift {total_drift}px across all lines; "
          f"worst '{worst[1]}' at {worst[0]}px")
    level_hlp = {"#": 255, "%": 160, "+": 80, ".": 0}
    for c, art in HLP_SYNTH.items():
        if c not in hlp:
            cols = [x for row in art for x, ch in enumerate(row) if ch == "#"]
            x0 = min(cols, default=0)
            x1 = max(cols, default=0) + 1
            hlp[c] = (x1 - x0, 0, x1 - x0,
                      [[(255, level_hlp[ch]) for ch in row[x0:x1]] for row in art])

    have = sorted(glyphs.keys())
    print("harvested:", "".join(have))
    # Audit EVERY string the page stages -- src/menu_assets.cpp is the truth.
    need = set()
    import re
    src = open("src/menu_assets.cpp", encoding="utf-8").read()
    for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', src):
        s = m.group(1)
        if any(ch.isalpha() for ch in s) and "%" not in s and "\\" not in s and "_" not in s and "/" not in s:
            need.update(s)
    for junk in ' <>@|':
        need.discard(junk)
    missing = sorted(c for c in need if c not in glyphs)
    print("missing:", "".join(missing) if missing else "(none)")
    # The help/description sentences render in the help face -- audit them
    # against it separately.
    helpneed = set()
    for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', src):
        s = m.group(1)
        if ("." in s) and (len(s) > 10) and any(ch.isalpha() for ch in s) \
                and ("\\" not in s) and ("_" not in s) and ("/" not in s) and ("%" not in s):
            helpneed.update(s)
    helpneed.discard(" ")
    hmissing = sorted(c for c in helpneed if c not in hlp)
    print("help missing:", "".join(hmissing) if hmissing else "(none)")
    if gaps:
        from collections import Counter
        print("letter gaps:", sorted(Counter(gaps).items()))
        print("space gaps:", sorted(__import__('collections').Counter(space_advances).items()))

    # Style references for the hand-derived glyphs.
    for ref in "MBoZvE1S8":
        if ref in glyphs:
            wdt, cs, ce, cell = glyphs[ref]
            print(f"--- {ref} (w={wdt} core {cs}..{ce}) ---")
            for row in cell:
                print("   ", "".join("#" if a >= 160 else ("+" if a else ".") for _i, a in row))

    # Preview sheet: every page string that exercises harvested + synthetic
    # glyphs, composed exactly the way the port will compose them.
    def compose(text_s, table=None, cell_h=None):
        table = glyphs if table is None else table
        cell_h = CELL_H if cell_h is None else cell_h
        LGAP, SGAP = 2, 4
        xc = 1
        placed = []
        for c in text_s:
            if c == " ":
                xc += SGAP
                continue
            if c not in table:
                xc += 6
                continue
            cw, cs, ce, cell = table[c]
            placed.append((xc - cs, cell, cw))
            xc += (ce - cs) + LGAP
        W = xc + 2
        out = [[(0, 0)] * W for _ in range(cell_h)]
        for px0, cell, cw in placed:
            for y in range(cell_h):
                for x in range(cw):
                    i, a = cell[y][x]
                    if a and (px0 + x) >= 0 and (px0 + x) < W and a >= out[y][px0 + x][1]:
                        out[y][px0 + x] = (i, a)
        return out, W

    if emit:
        from extract_menu_font import write_png
        tests = [("Graphics Options", hdr, HDR_CELL_H),
                 ("Widescreen Filter Left Fullscreen", None, None),
                 ("Anti-Aliasing Graphics Options", None, None),
                 ("1x 2x 3x 4x 5x 6x 7x 8x", None, None),
                 ("New Game 2D Detail Frame Rate", None, None),
                 ("Press Z Button to focus.", None, None),
                 ("Display and renderer settings.", hlp, HLP_CELL_H),
                 ("The N64 look, W3D 246 hxz Left Original Takes Widens.", hlp, HLP_CELL_H)]
        S = 6
        rows_png = []
        for t, tbl, ch in tests:
            out, W = compose(t, tbl, ch)
            for y in range(ch if ch else CELL_H):
                row = bytearray()
                for x in range(W):
                    i, a = out[y][x]
                    row += bytes(((i * a + 40 * (255 - a)) // 255,
                                  (i * a + 70 * (255 - a)) // 255,
                                  (i * a + 120 * (255 - a)) // 255, 255))
                rows_png.append(bytes(row))
            rows_png.append(bytes([20, 30, 50, 255]) * W)
        maxW = max(len(r) // 4 for r in rows_png)
        rows_png = [r + bytes([40, 70, 120, 255]) * (maxW - len(r) // 4) for r in rows_png]
        big = []
        for r in rows_png:
            px = [r[k:k + 4] for k in range(0, len(r), 4)]
            line = b"".join(p * S for p in px)
            for _ in range(S):
                big.append(line)
        write_png("build-win/Release/menu_text_reference/preview_font.png", maxW * S, len(big), big)
        print("wrote preview_font.png")

        import json
        data = {c: (wdt, cs, ce, [[(i, a) for (i, a) in row] for row in cell])
                for c, (wdt, cs, ce, cell) in glyphs.items()}
        data_hdr = {c: (wdt, cs, ce, [[(i, a) for (i, a) in row] for row in cell])
                    for c, (wdt, cs, ce, cell) in hdr.items()}
        with open("tools/menu_font_harvest.json", "w") as f:
            json.dump({"body": data, "hdr": data_hdr}, f)
        print("wrote tools/menu_font_harvest.json")

        # C header: index tables + IA blobs for both faces.
        def emit_table(f, name, table, cell_h):
            order = sorted(table.keys())
            blob = []
            entries = []
            for c in order:
                wdt, cs, ce, cell = table[c]
                off = len(blob) // 2
                for row in cell:
                    for (i, a) in row:
                        blob += [i, a]
                entries.append((c, wdt, cs, ce - cs, off))
            f.write("constexpr MenuGlyph k%sGlyphs[] = {\n" % name)
            for (c, wdt, cs, cw, off) in entries:
                if c == "'":
                    cc = "'\\''"
                elif c.isprintable() and ord(c) < 127:
                    cc = f"'{c}'"
                else:
                    cc = f"'\\x{ord(c):02x}'"
                f.write(f"    {{ {cc}, {wdt}, {cs}, {cw}, {off} }},\n")
            f.write("};\n")
            f.write("constexpr unsigned char k%sIA[] = {\n" % name)
            for k in range(0, len(blob), 24):
                f.write("    " + ",".join(str(v) for v in blob[k:k + 24]) + ",\n")
            f.write("};\n")
            return len(entries), len(blob)

        with open("src/menu_font.h", "w") as f:
            f.write("// The menu sprite fonts: letterforms harvested from the original\n")
            f.write("// pre-rendered menu sprites (plus a few drawn in their style: the\n")
            f.write("// characters no menu sprite contains). Body cells are %d rows,\n" % CELL_H)
            f.write("// header cells %d rows, of IA pairs.\n" % HDR_CELL_H)
            f.write("// Generated by tools/harvest_menu_font.py.\n")
            f.write("constexpr int kMenuFontCellH = %d;\n" % CELL_H)
            f.write("constexpr int kMenuHdrCellH = %d;\n" % HDR_CELL_H)
            f.write("constexpr int kMenuHlpCellH = %d;\n" % HLP_CELL_H)
            f.write("constexpr int kMenuFontLetterGap = 2;\n")
            f.write("constexpr int kMenuFontSpaceGap = 4;\n")
            # Measured across all 351 gaps of the twelve help sentences:
            # intra-letter gaps run 1-3 with mode 2, word spaces 4-7 with
            # mode 6.
            f.write("constexpr int kMenuHlpLetterGap = 2;\n")
            f.write("constexpr int kMenuHlpSpaceGap = 6;\n")
            f.write("struct MenuGlyph { char ch; unsigned char cellW, coreStart, coreW; unsigned short off; };\n")
            f.write("struct MenuKern { char a, b; unsigned char gap; };\n")
            n1, b1 = emit_table(f, "MenuFont", glyphs, CELL_H)
            n2, b2 = emit_table(f, "MenuHdr", hdr, HDR_CELL_H)
            n3, b3 = emit_table(f, "MenuCrd", crd, CELL_H)
            n4, b4 = emit_table(f, "MenuHlp", hlp, HLP_CELL_H)

            def emit_kern(name, table):
                f.write("// Per-pair gaps observed in the stock help sentences; pairs the\n")
                f.write("// sentences never set fall back to the face's default gap.\n")
                f.write("constexpr MenuKern k%s[] = {\n" % name)
                for (a, b), gap in sorted(table.items()):
                    f.write("    { '%s', '%s', %d },\n" % (a.replace("'", "\\'"), b.replace("'", "\\'"), gap))
                f.write("};\n")
            emit_kern("MenuHlpKern", hlp_kern)
            emit_kern("MenuHlpSpaceKern", hlp_space_kern)
        print("wrote src/menu_font.h (%d+%d+%d+%d glyphs, %d+%d+%d+%d bytes, %d+%d kern pairs)" %
              (n1, n2, n3, n4, b1, b2, b3, b4, len(hlp_kern), len(hlp_space_kern)))


if __name__ == "__main__":
    main()
