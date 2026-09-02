#!/usr/bin/env python3
"""Extract an original menu sprite from the ROM, pixel-exact, to PNG.

The main menu's images live VPK0-compressed at ROM 0xA0F830 and load at
VRAM 0x802B5000. A Sprite struct there points at a chain of Bitmap bands;
the pixel data is stored pre-shuffled for LoadBlockS (SP_TEXSHUF), so odd
rows have their 32-bit word pairs swapped and must be swapped back before
decoding. Formats seen here: CI (with RGBA16 TLUT), IA, I, RGBA.

Run under WSL from the decomp checkout so tools/vpk0_codec.py resolves:
  python3 extract_menu_sprite.py <rom> <vram_hex> <out.png> [scale]
"""
import os
import struct
import sys
import zlib

# The decomp checkout: SNAP_DECOMP if set, else ~/pokemonsnap (BUILDING.md).
sys.path.insert(0, os.path.join(os.path.expanduser(os.environ.get("SNAP_DECOMP", "~/pokemonsnap")), "tools"))
from vpk0_codec import decompress_vpk0

ROM_VPK0 = 0xA0F830
VPK_VRAM = 0x802B5000


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


def unshuffle(rowdata, rowbytes):
    """Undo the LoadBlockS odd-row shuffle: swap 32-bit word pairs."""
    out = bytearray(rowdata)
    for i in range(0, rowbytes - 7, 8):
        out[i:i+4], out[i+4:i+8] = rowdata[i+4:i+8], rowdata[i:i+4]
    return bytes(out)


def rgba16(px):
    r = (px >> 11) & 0x1F
    g = (px >> 6) & 0x1F
    b = (px >> 1) & 0x1F
    a = px & 1
    return ((r * 255) // 31, (g * 255) // 31, (b * 255) // 31, 255 * a)


def main():
    rom_path, vram, out_path = sys.argv[1], int(sys.argv[2], 16), sys.argv[3]
    scale = int(sys.argv[4]) if len(sys.argv) > 4 else 1

    with open(rom_path, "rb") as f:
        f.seek(ROM_VPK0)
        comp = f.read(0xA5CC46 - ROM_VPK0)
    seg, _ = decompress_vpk0(comp)
    print(f"decompressed segment: {len(seg)} bytes")

    def off(v):
        o = v - VPK_VRAM
        assert 0 <= o < len(seg), f"vram 0x{v:08X} outside segment"
        return o

    sp = off(vram)
    (x, y, width, height, scalex, scaley, expx, expy, attr, zdepth,
     red, green, blue, alpha, startTLUT, nTLUT, lut_ptr, istart, istep,
     nbitmaps, ndisplist, bmheight, bmHreal, bmfmt, bmsiz) = struct.unpack_from(
        ">hhhhffhhHhBBBBhhIhhhhhhBB", seg, sp)
    bitmap_ptr = struct.unpack_from(">I", seg, sp + 0x34)[0]
    print(f"sprite @0x{vram:08X}: {width}x{height} fmt={bmfmt} siz={bmsiz} "
          f"nbitmaps={nbitmaps} bmheight={bmheight}/{bmHreal} attr=0x{attr:04X} "
          f"lut=0x{lut_ptr:08X} nTLUT={nTLUT}")

    tlut = []
    if lut_ptr != 0 and nTLUT > 0:
        base = off(lut_ptr)
        for i in range(nTLUT):
            tlut.append(rgba16(struct.unpack_from(">H", seg, base + i * 2)[0]))

    bpt = {0: 0.5, 1: 1, 2: 2, 3: 4}[bmsiz]   # bytes per texel

    def decode(row, px):
        if bmsiz == 0:      # 4bpp
            byte = row[px // 2]
            v = (byte >> 4) if (px % 2 == 0) else (byte & 0xF)
            if bmfmt == 2:      # CI4
                return tlut[v] if v < len(tlut) else (0, 0, 0, 0)
            if bmfmt == 4:      # I4
                return (v * 17, v * 17, v * 17, 255)
            i3 = (v >> 1) * 36  # IA4
            return (i3, i3, i3, 255 * (v & 1))
        if bmsiz == 1:      # 8bpp
            v = row[px]
            if bmfmt == 2:      # CI8
                return tlut[v] if v < len(tlut) else (0, 0, 0, 0)
            if bmfmt == 4:      # I8
                return (v, v, v, 255)
            i4 = (v >> 4) * 17  # IA8
            return (i4, i4, i4, (v & 0xF) * 17)
        if bmsiz == 2:      # 16bpp
            v = struct.unpack_from(">H", row, px * 2)[0]
            if bmfmt == 3:      # IA16
                return (v >> 8, v >> 8, v >> 8, v & 0xFF)
            return rgba16(v)    # RGBA16
        r8, g8, b8, a8 = row[px*4:px*4+4]
        return (r8, g8, b8, a8)

    # Bitmaps tile HORIZONTALLY: each is a column band of the full height,
    # bw texels wide (bw_img the stored stride), exactly like the sprite
    # library walks them.
    img = [[(0, 0, 0, 0)] * width for _ in range(height)]
    x_cursor = 0
    for bi in range(nbitmaps):
        b = off(bitmap_ptr) + bi * 0x10
        bw, bw_img, bs, bt = struct.unpack_from(">hhhh", seg, b)
        buf = struct.unpack_from(">I", seg, b + 8)[0]
        actual_h = struct.unpack_from(">h", seg, b + 0xC)[0]
        rows = actual_h if actual_h > 0 else bmHreal
        rowbytes = int(bw_img * bpt)
        data = seg[off(buf): off(buf) + rowbytes * rows]
        print(f"  bitmap {bi}: bw={bw} stride={bw_img} rows={rows} buf=0x{buf:08X}")
        for ry in range(min(rows, height)):
            row = data[ry * rowbytes:(ry + 1) * rowbytes]
            if ry & 1:
                row = unshuffle(row, rowbytes)
            for px in range(min(bw, width - x_cursor)):
                img[ry][x_cursor + px] = decode(row, px)
        x_cursor += bw
        if x_cursor >= width:
            break

    out_w, out_h = width * scale, height * scale
    for suffix, background in ((".png", None), ("_on_blue.png", (40, 70, 120))):
        rows_png = []
        for yy in range(out_h):
            row = bytearray()
            for xx in range(out_w):
                r8, g8, b8, a8 = img[yy // scale][xx // scale]
                if background is None:
                    row += bytes((r8, g8, b8, a8))
                else:
                    br, bg, bb = background
                    row += bytes(((r8 * a8 + br * (255 - a8)) // 255,
                                  (g8 * a8 + bg * (255 - a8)) // 255,
                                  (b8 * a8 + bb * (255 - a8)) // 255, 255))
            rows_png.append(bytes(row))
        p = out_path[:-4] + suffix if suffix != ".png" else out_path
        write_png(p, out_w, out_h, rows_png)
        print(f"wrote {p} ({out_w}x{out_h})")


if __name__ == "__main__":
    main()
