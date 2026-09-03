#!/usr/bin/env python3
"""Write an "everything unlocked" copy of a Pokemon Snap save file.

    python tools/unlock_save.py build-win/Release/saves/pokemonsnap.bin out.bin

The save (saves/pokemonsnap.bin beside the executable) is a raw 128 KB image of
the cartridge's FlashRAM: one record of 0x1F2A4 bytes, a 16-byte checksum, the
magic "HAL_SNAP_V1.0-1", then the player's data (decomp src/more_funcs/
more_funcs.h, UnkBigBoy). This sets the highest-unlocked-course counter to
Rainbow Cloud, moves the course cursor with it, gives the Apple, Pester Ball,
Poke Flute and Dash Engine, marks the tutorial finished and the lab's full
button layout, and recomputes the checksum with the game's own MD4 variant
(src/more_funcs/md4.c: big-endian block words, a little-endian bit counter).
Everything else -- the Pokemon Report, the album, the print tray, the ending
flags -- is left exactly as it was, so the report fills and the ending plays
as the player earns them. The source file is never modified.
"""
import struct
import sys

M = 0xFFFFFFFF
RECORD = 0x1F2A4


def rol(v, s):
    return ((v << s) | (v >> (32 - s))) & M


def transform(st, block):
    X = struct.unpack('>16I', block)
    A, B, C, D = st
    F = lambda x, y, z: (x & y) | (~x & M & z)
    G = lambda x, y, z: (x & y) | (x & z) | (y & z)
    H = lambda x, y, z: x ^ y ^ z

    def step(a, b, c, d, k, s, const, op):
        return rol((a + op(b, c, d) + X[k] + const) & M, s)

    for r in range(4):
        A = step(A, B, C, D, 4 * r + 0, 3, 0, F)
        D = step(D, A, B, C, 4 * r + 1, 7, 0, F)
        C = step(C, D, A, B, 4 * r + 2, 11, 0, F)
        B = step(B, C, D, A, 4 * r + 3, 19, 0, F)
    for r in range(4):
        A = step(A, B, C, D, r + 0, 3, 0x5A827999, G)
        D = step(D, A, B, C, r + 4, 5, 0x5A827999, G)
        C = step(C, D, A, B, r + 8, 9, 0x5A827999, G)
        B = step(B, C, D, A, r + 12, 13, 0x5A827999, G)
    for r in (0, 2, 1, 3):
        A = step(A, B, C, D, r + 0, 3, 0x6ED9EBA1, H)
        D = step(D, A, B, C, r + 8, 9, 0x6ED9EBA1, H)
        C = step(C, D, A, B, r + 4, 11, 0x6ED9EBA1, H)
        B = step(B, C, D, A, r + 12, 15, 0x6ED9EBA1, H)
    return ((st[0] + A) & M, (st[1] + B) & M, (st[2] + C) & M, (st[3] + D) & M)


def hal_md4(data):
    st = (0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476)
    full = len(data) // 64
    for i in range(full):
        st = transform(st, data[i * 64:(i + 1) * 64])
    tail = data[full * 64:]
    buf = bytearray(64)
    buf[:len(tail)] = tail
    buf[len(tail)] = 0x80
    cnt = (len(data) * 8).to_bytes(8, 'little')
    if len(tail) < 0x38:
        buf[56:64] = cnt
        st = transform(st, bytes(buf))
    else:
        st = transform(st, bytes(buf))
        buf = bytearray(64)
        buf[56:64] = cnt
        st = transform(st, bytes(buf))
    return st


def checksum(buf):
    return struct.pack('>IIII', *hal_md4(bytes(buf[0x10:RECORD])))


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    buf = bytearray(open(src, 'rb').read())
    if len(buf) != 0x20000 or buf[0x10:0x1F] != b'HAL_SNAP_V1.0-1':
        sys.exit('%s is not a Pokemon Snap save (size or magic)' % src)
    if checksum(buf) != bytes(buf[0:0x10]):
        sys.exit('%s: the stored checksum does not match; refusing to edit a save the game would reject' % src)
    struct.pack_into('>I', buf, 0x50, 6)                   # highest course unlocked: Rainbow Cloud
    w = struct.unpack_from('>I', buf, 0x64)[0]
    struct.pack_into('>I', buf, 0x64, (w & ~(0x7 << 29)) | (6 << 29))   # course cursor with it
    buf[0x66] |= 0x80 | 0x40 | 0x20 | 0x04 | 0x01           # Apple, Pester Ball, Poke Flute, Dash Engine, lab layout
    buf[0x67] |= 0x80                                       # tutorial finished
    buf[0x68] |= 0x80                                       # a save exists (Continue)
    buf[0:0x10] = checksum(buf)
    open(dst, 'wb').write(buf)
    present = sum(1 for i in range(69) if struct.unpack_from('>i', buf, 0x180 + i * 0x3A0 + 4)[0] != -1)
    print('wrote %s: courses 0-6 open, all four items, tutorial done; report entries kept: %d of 69' % (dst, present))


if __name__ == '__main__':
    main()
