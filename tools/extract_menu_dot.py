#!/usr/bin/env python3
"""Extract the menu's furniture glyphs from the original sprites.

Decodes the "Screen" label (VRAM 0x8033F498) for the items' bullet dot and
the "Stereo" value strip (VRAM 0x80342FF0) for the two big chevron
brackets, straight out of the menu's VPK0 segment, and writes them as JSON
(dot, bracket L, bracket R, the text start column) to --out, by default
build-win/Release/menu_text_reference/menu_furniture.json -- never into the
tree. The port cuts the same pixels out of RDRAM at run time
(src/menu_harvest.cpp, harvest_furniture); this is the reference for that.

Run under WSL from the repo root:
  python3 tools/extract_menu_dot.py <rom> [--out <json path>]
"""
import os
import struct
import sys

# The decomp checkout: SNAP_DECOMP if set, else ~/pokemonsnap (BUILDING.md).
sys.path.insert(0, os.path.join(os.path.expanduser(os.environ.get("SNAP_DECOMP", "~/pokemonsnap")), "tools"))
from vpk0_codec import decompress_vpk0

ROM_VPK0 = 0xA0F830
VPK_VRAM = 0x802B5000
LABEL_VRAM = 0x8033F498    # "• Screen"
VALUE_VRAM = 0x80342FF0    # "< Stereo >"


def decode_sprite(seg, vram):
    off = lambda v: v - VPK_VRAM
    sp = off(vram)
    width, height = struct.unpack_from(">hh", seg, sp + 4)
    nbitmaps = struct.unpack_from(">h", seg, sp + 0x28)[0]
    bmHreal = struct.unpack_from(">h", seg, sp + 0x2E)[0]
    bitmap_ptr = struct.unpack_from(">I", seg, sp + 0x34)[0]

    img = [[(0, 0)] * width for _ in range(height)]
    x_cursor = 0
    for bi in range(nbitmaps):
        b = off(bitmap_ptr) + bi * 0x10
        bw, bw_img = struct.unpack_from(">hh", seg, b)
        buf = struct.unpack_from(">I", seg, b + 8)[0]
        rows = struct.unpack_from(">h", seg, b + 0xC)[0] or bmHreal
        rowbytes = bw_img * 2
        data = seg[off(buf): off(buf) + rowbytes * rows]
        for ry in range(min(rows, height)):
            row = bytearray(data[ry * rowbytes:(ry + 1) * rowbytes])
            if ry & 1:
                for i in range(0, rowbytes - 7, 8):
                    row[i:i+4], row[i+4:i+8] = row[i+4:i+8], row[i:i+4]
            for px in range(min(bw, width - x_cursor)):
                v = struct.unpack_from(">H", row, px * 2)[0]
                img[ry][x_cursor + px] = (v >> 8, v & 0xFF)
        x_cursor += bw
    return img, width, height


def col_core(img, width, height):
    return ["#" if any(img[y][x][1] >= 128 for y in range(height)) else "." for x in range(width)]


def crop(img, x0, x1, height):
    return [[img[y][x] for x in range(x0, x1)] for y in range(height)]


def main():
    rom_path = sys.argv[1]
    with open(rom_path, "rb") as f:
        f.seek(ROM_VPK0)
        comp = f.read(0xA5CC46 - ROM_VPK0)
    seg, _ = decompress_vpk0(comp)

    # Bullet dot: the run of solid columns before the label text.
    limg, lw, lh = decode_sprite(seg, LABEL_VRAM)
    core = col_core(limg, lw, lh)
    dot_end = core.index(".", core.index("#")) - 1
    text_start = dot_end + 1 + core[dot_end + 1:].index("#") - 1
    dot = crop(limg, 0, dot_end + 2, lh)
    print(f"dot cols 0..{dot_end + 1}, text starts {text_start}")

    # Brackets: first and last solid runs of the value strip.
    vimg, vw, vh = decode_sprite(seg, VALUE_VRAM)
    vcore = col_core(vimg, vw, vh)
    l_end = vcore.index(".", vcore.index("#")) - 1
    r_start = vw - 1 - vcore[::-1].index("#")
    while r_start > 0 and vcore[r_start - 1] == "#":
        r_start -= 1
    brk_l = crop(vimg, 0, l_end + 2, vh)
    brk_r = crop(vimg, max(0, r_start - 1), vw, vh)
    print(f"bracket L cols 0..{l_end + 1}, bracket R cols {r_start - 1}..{vw - 1}")

    import json

    def block(b):
        return {"w": len(b[0]), "h": len(b), "ia": [[i, a] for row in b for (i, a) in row]}

    out_path = "build-win/Release/menu_text_reference/menu_furniture.json"
    for i, arg in enumerate(sys.argv):
        if arg == "--out" and i + 1 < len(sys.argv):
            out_path = sys.argv[i + 1]
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "w") as f:
        json.dump({"dot_text_start": text_start, "dot": block(dot),
                   "brk_l": block(brk_l), "brk_r": block(brk_r)}, f)
    print("wrote", out_path)


if __name__ == "__main__":
    main()
