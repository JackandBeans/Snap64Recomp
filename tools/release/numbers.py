"""The suite's numbers into the pages: docs/VERIFICATION.md's paragraph on the executable built here and
the README's sentence, from the local suite logs and the executable's hash, the previous release's numbers
folded into the paragraph's history. Refuses a log with a failure. Keeps each file's line endings.

    tools/release/numbers.py <version> <suite log> <station log> [<executable>]"""
import hashlib
import io
import os
import re
import subprocess
import sys
import textwrap

if len(sys.argv) < 4:
    sys.exit(__doc__)
V, SUITE, STATION = sys.argv[1], sys.argv[2], sys.argv[3]
ROOT = subprocess.run(['git', 'rev-parse', '--show-toplevel'], capture_output=True, text=True).stdout.strip()
EXE = sys.argv[4] if len(sys.argv) > 4 else os.path.join(ROOT, 'build-win', 'Release', 'Snap64Recomp.exe')


def tally(path):
    text = io.open(path, encoding='utf-8', errors='replace').read()
    m = re.search(r'^(\d+) checks, (\d+) failed, (\d+) s$', text, re.M)
    assert m, path
    assert int(m.group(2)) == 0, (path, [l for l in text.splitlines() if l.startswith('FAIL')])
    return int(m.group(1)), int(m.group(3))


checks, secs = tally(SUITE)
st, st_secs = tally(STATION)
exe = hashlib.sha256(open(EXE, 'rb').read()).hexdigest()[:8]
print('suite %d in %d s; station %d in %d s; exe %s' % (checks, secs, st, st_secs, exe))


def rewrite(rel, fn):
    path = os.path.join(ROOT, rel)
    raw = io.open(path, encoding='utf-8', newline='').read()
    crlf = '\r\n' in raw
    s = fn(raw.replace('\r\n', '\n'))
    io.open(path, 'w', encoding='utf-8', newline='').write(s.replace('\n', '\r\n') if crlf else s)
    print('ok', rel)


def verification(s):
    pat = re.compile(r'  On the (\S+) executable built here \(SHA-256 beginning `(\w+)`\), run\n'
                     r'  without diagnostics in the environment, the suite passed (\d+) of (\d+) checks\n'
                     r'  in (\d+) seconds(.*?), and the station\'s (\d+) of (\d+)\n?\s*in (\d+); the ', re.S)
    m = pat.search(s)
    assert m, 'the paragraph on the executable built here was not found'
    prev, p_checks, p_total, p_secs, p_st, p_st_secs = m.group(1), m.group(3), m.group(4), m.group(5), m.group(7), m.group(9)
    assert prev != V, 'the pages already quote %s' % V
    new = ('On the %s executable built here (SHA-256 beginning `%s`), run without diagnostics in the environment, '
           'the suite passed %d of %d checks in %d seconds, and the station\'s %d of %d in %d; the %s executable had '
           'passed %s of %s in %s and the station\'s %s in %s, the ' % (V, exe, checks, checks, secs, st, st, st_secs, prev, p_checks, p_total, p_secs, p_st, p_st_secs))
    wrapped = '\n'.join('  ' + l for l in textwrap.wrap(new, 76))
    if not wrapped.endswith(' '):
        wrapped += ' '
    return s[:m.start()] + wrapped + s[m.end():]


def readme(s):
    pat = re.compile(r'\(`tools/release_check\.py`\)\. On the (\S+) executable it passed (\d+) of (\d+)\n'
                     r'checks, and the station\'s (\d+) of (\d+)\.')
    m = pat.search(s)
    assert m, 'the README sentence was not found'
    return s[:m.start()] + ('(`tools/release_check.py`). On the %s executable it passed %d of %d\nchecks, and the station\'s %d of %d.'
                            % (V, checks, checks, st, st)) + s[m.end():]


rewrite('docs/VERIFICATION.md', verification)
rewrite('README.md', readme)
