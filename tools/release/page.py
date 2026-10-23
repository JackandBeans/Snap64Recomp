"""The release page on GitHub, made and checked the house way.

    tools/release/page.py create  <version> <notes file> <archives dir>   a draft with the twelve files
    tools/release/page.py check   <version> <notes file> <archives dir> [--download]
    tools/release/page.py publish <version>                                latest, not a prerelease
    tools/release/page.py pictures <version>                               every picture of the rendered page fetched

create and publish are outward: they need the go marker .claude/GO (made on the author's word, less than
three hours old). Environment: SNAP64_REPO (default from origin)."""
import hashlib
import io
import json
import os
import re
import subprocess
import sys
import time
import urllib.request

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
ROOT = subprocess.run(['git', 'rev-parse', '--show-toplevel'], capture_output=True, text=True).stdout.strip()
REPO = os.environ.get('SNAP64_REPO') or subprocess.run(['git', 'remote', 'get-url', 'origin'], capture_output=True, text=True).stdout.strip().split('github.com')[-1].lstrip(':/').removesuffix('.git')
if len(sys.argv) < 3:
    sys.exit(__doc__)
CMD, V = sys.argv[1], sys.argv[2]
TAG = 'v' + V
NAMES = ['Snap64Recomp-%s-%s' % (V, kind) for kind in ('win64.zip', 'linux-x86_64.tar.gz', 'linux-x86_64.flatpak', 'linux-arm64.tar.gz', 'macos-universal.zip', 'android.apk')]


def go_given():
    marker = os.path.join(ROOT, '.claude', 'GO')
    try:
        return time.time() - os.path.getmtime(marker) < 3 * 3600
    except OSError:
        return False


def gh(*args):
    r = subprocess.run(['gh'] + list(args), capture_output=True)
    if r.returncode != 0:
        sys.exit('gh %s failed: %s' % (' '.join(args[:3]), r.stderr.decode('utf-8', 'replace')[:400]))
    return r.stdout.decode('utf-8', 'replace')


def release():
    rels = json.loads(gh('api', 'repos/%s/releases?per_page=10' % REPO))
    for x in rels:
        if x['tag_name'] == TAG:
            return x
    return None


if CMD == 'create':
    notes, arch = sys.argv[3], sys.argv[4].rstrip('/\\') + '/'
    text = io.open(notes, encoding='utf-8').read()
    assert '{' not in text, 'the notes still have a placeholder'
    files = []
    for n in NAMES:
        for f in (arch + n, arch + n + '.sha256'):
            assert os.path.exists(f), 'missing ' + f
            files.append(f)
    if release() is not None:
        sys.exit('the %s release exists already' % TAG)
    assert go_given(), 'no go marker: the page is not made without the author\'s go (touch .claude/GO on his word)'
    print(gh('release', 'create', TAG, '-R', REPO, '--draft', '--title', 'Snap64 Recomp ' + V, '--notes-file', notes, '--verify-tag', *files).strip())
    r = release()
    print('draft %s, %d assets, id %s' % (r['draft'], len(r['assets']), r['id']))

elif CMD == 'check':
    notes, arch = sys.argv[3], sys.argv[4].rstrip('/\\') + '/'
    r = release()
    assert r is not None, 'no %s release' % TAG
    want = io.open(notes, encoding='utf-8').read().replace('\r\n', '\n').strip()
    print('draft %s | prerelease %s | %s' % (r['draft'], r['prerelease'], r['html_url']))
    print('body reads back the same:', r['body'].replace('\r\n', '\n').strip() == want)
    ok = True
    for a in sorted(r['assets'], key=lambda a: a['name']):
        local = hashlib.sha256(open(arch + a['name'], 'rb').read()).hexdigest()
        same = (a.get('digest') or '') == 'sha256:' + local and a['size'] == os.path.getsize(arch + a['name'])
        line = '  %-48s %9d  %s  %s' % (a['name'], a['size'], local[:16], 'same as local' if same else 'DIFFERENT')
        if '--download' in sys.argv and not r['draft']:
            got = hashlib.sha256(urllib.request.urlopen(a['browser_download_url']).read()).hexdigest()
            line += ' | downloaded ' + ('same' if got == local else 'DIFFERENT')
            same = same and got == local
        ok = ok and same
        print(line)
    print('ALL SAME' if ok and len(r['assets']) == 2 * len(NAMES) else 'SOMETHING DIFFERS')

elif CMD == 'publish':
    r = release()
    assert r is not None and r['draft'], 'no draft %s release to publish' % TAG
    assert go_given(), 'no go marker: nothing is published without the author\'s go (touch .claude/GO on his word)'
    gh('release', 'edit', TAG, '-R', REPO, '--draft=false', '--latest')
    latest = json.loads(gh('api', 'repos/%s/releases/latest' % REPO))
    print('latest: %s draft %s prerelease %s published %s' % (latest['tag_name'], latest['draft'], latest['prerelease'], latest['published_at']))

elif CMD == 'pictures':
    html = json.loads(gh('api', '-H', 'Accept: application/vnd.github.html+json', 'repos/%s/releases/tags/%s' % (REPO, TAG)))['body_html']
    imgs = re.findall(r'<img[^>]*>', html)
    print('pictures in the rendered page:', len(imgs))
    for tag in imgs:
        src = re.search(r'src="([^"]+)"', tag).group(1).replace('&amp;', '&')
        orig = re.search(r'data-canonical-src="([^"]+)"', tag)
        with urllib.request.urlopen(urllib.request.Request(src, headers={'User-Agent': 'Mozilla/5.0'})) as f:
            data = f.read()
            print('  %s  %d bytes  %s  %s' % (f.status, len(data), f.headers.get('Content-Type'), (orig.group(1) if orig else src).rsplit('/', 1)[-1]))
else:
    sys.exit(__doc__)
