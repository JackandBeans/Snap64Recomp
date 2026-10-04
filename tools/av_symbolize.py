"""Names the functions behind a [SNAP-AV] line's host stack, from the MSVC map shipped beside the
executable: python tools/av_symbolize.py <Snap64Recomp.map of that build> <rva> <rva> ..."""
import bisect
import io
import re
import subprocess
import sys

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
path = sys.argv[1]
syms = []
for line in open(path, encoding='utf-8', errors='replace'):
    m = re.match(r'\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{16})\s+(f\s+)?(i\s+)?(\S+)?', line)
    if m:
        addr = int(m.group(2), 16)
        if addr >= 0x140000000:
            syms.append((addr - 0x140000000, m.group(1), m.group(5) or ''))
syms.sort()
keys = [s[0] for s in syms]
print(len(syms), 'symbols from', path)


def undecorate(name):
    try:
        r = subprocess.run(['undname', name], capture_output=True, text=True, timeout=10)
        m = re.search(r'is :- "(.*)"', r.stdout)
        if m:
            return m.group(1)
    except Exception:
        pass
    return name


for a in sys.argv[2:]:
    rva = int(a, 16)
    i = bisect.bisect_right(keys, rva) - 1
    if i < 0:
        print('%8X  ?' % rva)
        continue
    base, name, obj = syms[i]
    print('%8X  +0x%-5X %s   [%s]' % (rva, rva - base, undecorate(name)[:170], obj))
