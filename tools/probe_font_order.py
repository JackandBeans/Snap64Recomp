#!/usr/bin/env python3
"""Try candidate bit/byte orders for the 12px atlas and render a comparison.

One row per hypothesis, glyphs 'A B C D a b c 1 2' at 12x, labeled by order:
  0: as-read              (high nibble = left pixel, bytes as in file)
  1: nibble swap          (low nibble = left pixel)
  2: byte pair swap       (offset ^ 1 within the glyph)
  3: 32-bit word reverse  (offset ^ 3 within the glyph)
  4: byte pair + nibble
  5: word reverse + nibble
"""
import struct
import sys
import zlib

ROM_ATLAS_12 = 0x84BA04
GLYPH_BYTES = 72
GLYPH_SIZE = 12
GLYPHS = [272, 273, 274, 275, 298, 299, 300, 324, 325]
SCALE = 12


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


def texel(atlas, glyph, x, y, order):
    off = y * (GLYPH_SIZE // 2) + x // 2
    if order in (2, 4):
        off ^= 1
    if order in (3, 5):
        off ^= 3
    packed = atlas[glyph * GLYPH_BYTES + off]
    if order in (1, 4, 5):
        nib = (packed >> 4) if (x & 1) else (packed & 0xF)
    else:
        nib = (packed & 0xF) if (x & 1) else (packed >> 4)
    return nib * 17


def main():
    rom_path, out_path = sys.argv[1], sys.argv[2]
    with open(rom_path, "rb") as f:
        f.seek(ROM_ATLAS_12)
        atlas = f.read(400 * GLYPH_BYTES)

    cell = GLYPH_SIZE + 2
    cols = len(GLYPHS)
    rows_n = 6
    w, h = cols * cell * SCALE, rows_n * cell * SCALE
    img = [[(24, 34, 60, 255)] * w for _ in range(h)]
    for order in range(rows_n):
        for gi, g in enumerate(GLYPHS):
            ox = gi * cell + 1
            oy = order * cell + 1
            for gy in range(GLYPH_SIZE):
                for gx in range(GLYPH_SIZE):
                    v = texel(atlas, g, gx, gy, order)
                    if v == 0:
                        continue
                    for dy in range(SCALE):
                        for dx in range(SCALE):
                            img[(oy + gy) * SCALE + dy][(ox + gx) * SCALE + dx] = (v, v, v, 255)
    rows = [b"".join(bytes(px) for px in r) for r in img]
    write_png(out_path, w, h, rows)
    print("wrote", out_path)


if __name__ == "__main__":
    main()
