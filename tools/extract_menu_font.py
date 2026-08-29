#!/usr/bin/env python3
"""Pixel-exact reference renders of the GRAPHICS menu strings.

Mirrors src/menu_assets.cpp byte for byte: the same ROM offsets, the same
glyph indexing, the same advances, the same +2,+2 drop shadow and bracket
chevrons. The output is what the game itself composites -- scaled up with
nearest-neighbour so an image generator can copy the letterforms exactly.

Usage: python tools/extract_menu_font.py <path-to-pokemonsnap.z64> <outdir>
"""
import os
import struct
import sys
import zlib

ROM_ATLAS_12 = 0x84BA04    # charset 1, the large 12px glyphs
ROM_ATLAS_8 = 0x848BC0     # charset 0, the small 8px glyphs the menus use
GLYPH_COUNT = 370
GLYPH_SIZE = 8
STRIP_HEIGHT = GLYPH_SIZE + 2
SCALE = 6

# UIText_WidthTable[0] from the decomp (src/window/text.c), indices 272..369:
# the 8px charset's advance widths. The game draws at width + 1.
WIDTHS_272 = [
    5, 5, 5, 5, 5, 5, 5, 5,             # A-H
    3, 5, 5, 4, 5, 5, 5, 5, 5, 5,       # I-R
    4, 5, 5, 5, 5, 5, 5, 5,             # S-Z
    5, 4,                               # a b
    3, 4, 4, 4, 4, 4, 2, 3, 4, 1,       # c-l
    5, 4, 4, 3, 4, 3, 3, 4, 4, 4,       # m-v
    5, 4, 4, 4,                         # w-z
    3, 5, 5, 5, 5, 5, 5, 5, 5,          # 1-9
    5,                                  # 0
    5, 5, 5, 2, 5, 5, 4, 5, 5, 5,       # arrows, !, @, #, $, %, &, *
    2, 2, 4, 5, 3, 3,                   # ( ) _ + = -
    1, 3, 2, 1, 3, 1, 2, 4, 3, 3,       # ' " ; : / . , ? < >
    5, 5, 5, 5, 5, 2, 2, 4, 5, 5,       # symbols
]

STRINGS = {
    "graphics": "Graphics",
    "render_scale": "Render Scale",
    "anti_aliasing": "Anti-Aliasing",
    "widescreen": "Widescreen",
    "frame_rate": "Frame Rate",
    "2d_detail": "2D Detail",
    "filter": "Filter",
    "dither": "Dither",
    "fullscreen": "Fullscreen",
    "item_help": "Display and renderer settings.",
    "page_help": "Left and Right change the setting.",
    "auto": "< Auto >",
    "off": "< Off >",
    "on": "< On >",
    "1x": "< 1x >", "2x": "< 2x >", "3x": "< 3x >", "4x": "< 4x >",
    "5x": "< 5x >", "6x": "< 6x >", "7x": "< 7x >", "8x": "< 8x >",
    "original": "< Original >",
    "display": "< Display >",
    "classic": "< Classic >",
    "sharp": "< Sharp >",
    "point": "< Point >",
    "smooth": "< Smooth >",
    "crisp": "< Crisp >",
}


def write_png(path, width, height, rgba_rows):
    raw = b"".join(b"\x00" + row for row in rgba_rows)
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def index_of(c):
    if "A" <= c <= "Z":
        return 272 + (ord(c) - ord("A"))
    if "a" <= c <= "z":
        return 298 + (ord(c) - ord("a"))
    if "1" <= c <= "9":
        return 324 + (ord(c) - ord("1"))
    if c == "0":
        return 333
    if c == "-":
        return 349
    if c == ":":
        return 353
    if c == ".":
        return 355
    return -1


# The bullet dot and the value chevrons, lifted from the original sprites
# by tools/extract_menu_dot.py. The chevrons are far larger than the
# charset's own bracket glyphs, and the originals carry their shadows.
try:
    import menu_glyphs
    BRACKETS = {"<": menu_glyphs.BRK_L, ">": menu_glyphs.BRK_R}
except ImportError:
    BRACKETS = {}


def texel(atlas, glyph, x, y):
    # Left pixel of each pair is the LOW nibble -- confirmed by the game's
    # own blitter, UIText_PrintChar16 in src/window/text.c.
    packed = atlas[glyph * (GLYPH_SIZE * GLYPH_SIZE // 2) + y * (GLYPH_SIZE // 2) + x // 2]
    nib = (packed >> 4) if (x & 1) else (packed & 0xF)
    return nib * 17


def glyph_advance(c):
    if c == " ":
        return 3
    if c in BRACKETS:
        return len(BRACKETS[c][0])
    idx = index_of(c)
    if idx < 0 or not (272 <= idx <= 369):
        return GLYPH_SIZE
    # The game's own rule: UIText_WidthTable[0][index] + 1.
    return WIDTHS_272[idx - 272] + 1


def compose(widths, atlas, text):
    """UIText_PrintChar16's output, as an IA16 strip: per glyph, a solid
    black shadow texel at +1,+1 and a coverage-graded one at +2,+2, then the
    glyph itself in white. `widths` is accepted for compatibility; the
    advances come from the table in this file."""
    width = 2 + sum(glyph_advance(c) for c in text)
    intensity = bytearray(width * STRIP_HEIGHT)
    alpha = bytearray(width * STRIP_HEIGHT)

    def blend(px, py, i, a):
        if px < 0 or px >= width or py < 0 or py >= STRIP_HEIGHT or a == 0:
            return
        at = py * width + px
        if a >= alpha[at]:
            intensity[at] = max(intensity[at], i)
            alpha[at] = a

    x = 0
    for c in text:
        if c == " ":
            x += 3
            continue
        if c in BRACKETS:
            block = BRACKETS[c]
            for gy in range(min(len(block), STRIP_HEIGHT)):
                for gx in range(len(block[0])):
                    bi, ba = block[gy][gx]
                    blend(x + gx, gy, bi, ba)
            x += len(block[0])
            continue
        idx = index_of(c)
        if idx < 0:
            x += GLYPH_SIZE
            continue
        for p in range(2):
            for gy in range(GLYPH_SIZE):
                for gx in range(GLYPH_SIZE):
                    a = texel(atlas, idx, gx, gy)
                    if a == 0:
                        continue
                    if p == 0:
                        # A thin graded shadow at +1,+1: what the stock menu
                        # glyphs show on screen (crop-compared against the
                        # rendered "Sound" label).
                        blend(x + gx + 1, gy + 1, 0, a)
                    else:
                        blend(x + gx, gy, 0xFF, a)
        x += glyph_advance(c)
    return width, intensity, alpha


def save_strip(path, width, intensity, alpha, scale, background=None):
    w, h = width * scale, STRIP_HEIGHT * scale
    rows = []
    for y in range(h):
        row = bytearray()
        sy = y // scale
        for x in range(w):
            sx = x // scale
            at = sy * width + sx
            i, a = intensity[at], alpha[at]
            if background is None:
                row += bytes((i, i, i, a))
            else:
                br, bg, bb = background
                r = (i * a + br * (255 - a)) // 255
                g = (i * a + bg * (255 - a)) // 255
                b = (i * a + bb * (255 - a)) // 255
                row += bytes((r, g, b, 255))
        rows.append(bytes(row))
    write_png(path, w, h, rows)


def save_atlas_sheet(path, widths, atlas, scale=4, cols=20):
    cell = GLYPH_SIZE + 2
    rows_n = (GLYPH_COUNT + cols - 1) // cols
    w, h = cols * cell * scale, rows_n * cell * scale
    img = [[(24, 34, 60, 255)] * w for _ in range(h)]
    for g in range(GLYPH_COUNT):
        ox = (g % cols) * cell + 1
        oy = (g // cols) * cell + 1
        for gy in range(GLYPH_SIZE):
            for gx in range(GLYPH_SIZE):
                v = texel(atlas, g, gx, gy)
                if v == 0:
                    continue
                for dy in range(scale):
                    for dx in range(scale):
                        img[(oy + gy) * scale + dy][(ox + gx) * scale + dx] = (v, v, v, 255)
    rows = [b"".join(bytes(px) for px in r) for r in img]
    write_png(path, w, h, rows)


def main():
    rom_path, outdir = sys.argv[1], sys.argv[2]
    os.makedirs(outdir, exist_ok=True)
    with open(rom_path, "rb") as f:
        f.seek(ROM_ATLAS_8)
        atlas = f.read(GLYPH_COUNT * (GLYPH_SIZE * GLYPH_SIZE // 2))

    for name, text in STRINGS.items():
        width, intensity, alpha = compose(None, atlas, text)
        save_strip(os.path.join(outdir, f"{name}.png"), width, intensity, alpha, SCALE)
        save_strip(os.path.join(outdir, f"{name}_on_blue.png"), width, intensity, alpha,
                   SCALE, background=(40, 70, 120))
    save_atlas_sheet(os.path.join(outdir, "atlas_8px.png"), None, atlas)
    print(f"wrote {len(STRINGS)} strings x2 + atlas sheet to {outdir}")


if __name__ == "__main__":
    main()
