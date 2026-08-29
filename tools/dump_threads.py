#!/usr/bin/env python3
"""Where is every thread? Minidump -> per-thread RIP + stack return addresses,
symbolized against the MSVC linker map. No debugger required.

Usage: python tools/dump_threads.py <dump> <exe_map>
"""
import bisect
import struct
import sys

STREAM_THREAD_LIST = 3
STREAM_MODULE_LIST = 4
STREAM_MEMORY64_LIST = 9
MAP_PREFERRED_BASE = 0x140000000


def load_map_symbols(path):
    syms = []
    with open(path, "r", errors="replace") as f:
        for line in f:
            parts = line.split()
            # 0001:002b6420  error_wait  00000001402b7420 f  obj
            if len(parts) >= 3 and ":" in parts[0]:
                try:
                    va = int(parts[2], 16)
                except ValueError:
                    continue
                if va >= MAP_PREFERRED_BASE:
                    syms.append((va, parts[1]))
    syms.sort()
    return syms


def symbolize(syms, va):
    i = bisect.bisect_right(syms, (va, "\xff")) - 1
    if i < 0:
        return "?"
    base, name = syms[i]
    return f"{name}+0x{va - base:X}"


def main():
    dump_path, map_path = sys.argv[1], sys.argv[2]
    syms = load_map_symbols(map_path)
    f = open(dump_path, "rb")

    sig, _ver, nstreams, dir_rva = struct.unpack("<IIII", f.read(16))
    assert sig == 0x504D444D, "not a minidump"
    f.seek(dir_rva)
    streams = {}
    for _ in range(nstreams):
        stype, dsize, rva = struct.unpack("<III", f.read(12))
        streams[stype] = (dsize, rva)

    # Executable base: the first module.
    _, mrva = streams[STREAM_MODULE_LIST]
    f.seek(mrva)
    (nmod,) = struct.unpack("<I", f.read(4))
    f.seek(mrva + 4)
    exe_base, exe_size = None, None
    mods = []
    for _ in range(nmod):
        rec = f.read(108)
        base, size = struct.unpack_from("<QI", rec, 0)
        name_rva = struct.unpack_from("<I", rec, 20)[0]
        mods.append((base, size, name_rva))
    here = f.tell()
    named = []
    for base, size, name_rva in mods:
        f.seek(name_rva)
        (chars,) = struct.unpack("<I", f.read(4))
        name = f.read(chars).decode("utf-16-le", errors="replace")
        named.append((base, size, name))
    f.seek(here)
    exe_base, exe_size = named[0][0], named[0][1]
    print(f"exe: {named[0][2]}  base=0x{exe_base:X} size=0x{exe_size:X}")

    # Memory64: sorted ranges with a running file offset for stack reads.
    mem_ranges = []
    if STREAM_MEMORY64_LIST in streams:
        _, rva = streams[STREAM_MEMORY64_LIST]
        f.seek(rva)
        nrange, base_rva = struct.unpack("<QQ", f.read(16))
        off = base_rva
        for _ in range(nrange):
            start, size = struct.unpack("<QQ", f.read(16))
            mem_ranges.append((start, size, off))
            off += size
    mem_starts = [r[0] for r in mem_ranges]

    def read_mem(addr, size):
        i = bisect.bisect_right(mem_starts, addr) - 1
        if i < 0:
            return b""
        start, rsize, roff = mem_ranges[i]
        if addr >= start + rsize:
            return b""
        take = min(size, start + rsize - addr)
        f.seek(roff + (addr - start))
        return f.read(take)

    def in_exe(va):
        return exe_base <= va < exe_base + exe_size

    def sym(va):
        return symbolize(syms, va - exe_base + MAP_PREFERRED_BASE)

    _, trva = streams[STREAM_THREAD_LIST]
    f.seek(trva)
    (nthreads,) = struct.unpack("<I", f.read(4))
    threads = []
    for _ in range(nthreads):
        rec = f.read(48)
        tid = struct.unpack_from("<I", rec, 0)[0]
        ctx_size, ctx_rva = struct.unpack_from("<II", rec, 40)
        threads.append((tid, ctx_size, ctx_rva))

    for tid, ctx_size, ctx_rva in threads:
        f.seek(ctx_rva)
        ctx = f.read(ctx_size)
        rip = struct.unpack_from("<Q", ctx, 0xF8)[0]
        rsp = struct.unpack_from("<Q", ctx, 0x98)[0]
        where = sym(rip) if in_exe(rip) else next(
            (f"[{n.rsplit(chr(92), 1)[-1]}]" for b, s, n in named if b <= rip < b + s), "?")
        print(f"tid={tid:6d} rip=0x{rip:X} {where}")
        if in_exe(rip):
            stack = read_mem(rsp, 0x1200)
            frames = []
            for i in range(0, len(stack) - 7, 8):
                v = struct.unpack_from("<Q", stack, i)[0]
                if in_exe(v):
                    frames.append(sym(v))
                if len(frames) >= 12:
                    break
            print("        stack:", " <- ".join(frames))


if __name__ == "__main__":
    main()
