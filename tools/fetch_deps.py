#!/usr/bin/env python3
"""Fetch the trees a clean checkout of Snap64 Recomp does not carry.

Every third-party tree is a git submodule at an exact commit: `lib/rt64` and
`lib/N64ModernRuntime` on Snap64 Recomp's forks (branch `snap64`, the
upstream commit plus the port's changes), `lib/SDL` and `lib/DirectX-Headers`
on their upstreams, and everything nested in them (RT64's `src/contrib`, the
runtime's N64Recomp and third-party trees) at the commits those repositories
record. This script does what `git submodule update --init --recursive`
does, checks that every checkout sits at its recorded commit, and then does
the one thing git cannot: it verifies RT64's dxc binaries by SHA-256 and puts
Microsoft's v1.7.2308 `dxil.dll` in place of the one dxc-bin carries
(`DXIL_OVERLAY` below says why).

    python tools/fetch_deps.py            # fetch what is missing, verify
    python tools/fetch_deps.py --list     # print every submodule's commit and exit
    python tools/fetch_deps.py --full     # whole histories instead of one commit each

Afterwards each submodule is detached at its recorded commit, and each one
on a fork of the project's also has the fork's `snap64` branch ref, so
`git switch snap64` in it works (VENDORING.md, "Changing RT64 or the
runtime").

Needs Python 3 (standard library only), git on PATH and network access to
github.com. It is idempotent: a checkout already at its commit is left
alone, and a second run does nothing. The submodules are fetched one commit
deep unless --full is given; `git fetch --unshallow` inside one gives it its
history later (VENDORING.md, "Changing RT64 or the runtime").
"""
import argparse
import hashlib
import os
import pathlib
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import urllib.request
import zipfile

REPO = pathlib.Path(__file__).resolve().parent.parent
DXC = REPO / 'lib' / 'rt64' / 'src' / 'contrib' / 'dxc'

# Every file of dxc-bin at the pin, with its SHA-256 and where it came from.
# "release" names the Microsoft DirectXShaderCompiler GitHub release asset the
# file is byte-identical to; "private" means no published asset matches
# (details and the evidence in VENDORING.md). The Windows build uses
# bin/x64/dxc.exe (shader compiler, at build time), bin/x64/dxcompiler.dll and
# bin/x64/dxil.dll (beside the executable), inc/*.h and lib/x64/dxcompiler.lib.
DXC_FILES = {
    'bin/x64/dxil.dll': ('9cccc7ef419da73fa314fdaecae831c6c20206ae70732c9093f95193378ced10',
                         'release v1.7.2308 dxc_2023_08_14.zip (bin/x64/dxil.dll, 101.7.2308.12); '
                         'put in place of dxc-bin\'s v1.7.2212 file by DXIL_OVERLAY'),
    'bin/x64/dxc.exe': ('94cf9978834c5d44fd42f3a5ee964e2293a57efa455d05e3a13482c46b714b6f',
                        'private build 1.7.0.4147 of DXC main commit 0dc8d9060 (2023-10-09); no release asset matches'),
    'bin/x64/dxcompiler.dll': ('15304a82c8a61db615a83961d8967a5c468ae7ce77b86d8e9b3dec60f7ae2166',
                               'private build 1.7.0.4147 of DXC main commit 0dc8d9060 (2023-10-09); no release asset matches'),
    'lib/x64/dxcompiler.lib': ('34237369336dd9a08916349449e7f8ee525a1cd17657c1fc40fd52bf8bb960f2',
                               'private (import library of the private dxcompiler.dll; matches no release .lib)'),
    'inc/dxcapi.h': ('165a39385bbd3c90203f2891af377bcc148275b5212769ad53591bc43e249f44',
                     'release v1.8.2403.2 linux_dxc_2024_03_29.x86_64.tar.gz (include/dxc/dxcapi.h)'),
    'inc/dxcerrors.h': ('70e138de3511267a2163d433e576c38fe8467f18ee7b8ea4970e7a3236053feb',
                        'release v1.8.2403.2 linux_dxc_2024_03_29.x86_64.tar.gz (include/dxc/dxcerrors.h)'),
    'inc/dxcisense.h': ('0091a2e8cbd90d7432a34778a509457069046cb8d428329e19a465ca2cb1e684',
                        'release v1.8.2403.2 linux_dxc_2024_03_29.x86_64.tar.gz (include/dxc/dxcisense.h)'),
    'inc/WinAdapter.h': ('82fd7067521b71fdc052986608191a8817a87301f1ca83d31a795f2a7f419301',
                         'release v1.8.2403.2 linux_dxc_2024_03_29.x86_64.tar.gz (include/dxc/WinAdapter.h)'),
    'bin/x64/dxc-linux': ('4e6f4e52989aca69739880b40b9f988357f15d10ca03284377b81f1502463ff5',
                          'release v1.8.2403.2 linux_dxc_2024_03_29.x86_64.tar.gz (bin/dxc)'),
    'lib/x64/libdxcompiler.so': ('f875d0a52f4e69e9de999b7f749414bdc529aa04e9b1bc75802453f81fcd20df',
                                 'release v1.8.2403.2 linux_dxc_2024_03_29.x86_64.tar.gz (lib/libdxcompiler.so)'),
    'lib/x64/libdxil.so': ('27bed3596bebd053dd56c5542233e83b87820ccb531fbf5438d54de1d9952acd',
                           'release v1.8.2403.2 linux_dxc_2024_03_29.x86_64.tar.gz (lib/libdxil.so)'),
    'bin/arm64/dxc-linux': ('07fefd4c02afeecf37f614b6670ed2cbfae520aba9520c94bb04e4ed135e7863',
                            'private (embeds 1.8.2403.2; Microsoft publishes no arm64 Linux asset)'),
    'lib/arm64/libdxcompiler.so': ('e1f5a8debc11eac62bfe7f64417dba13eae21c014387787e6bdabf5324942364',
                                   'private (embeds 1.8.2403.2; Microsoft publishes no arm64 Linux asset)'),
    'bin/x64/dxc-macos': ('2af2c472837d98ff584328b163d12b5b65a93e9706fd7d858c9aba79d06654f0',
                          'LunarG Vulkan SDK build (embeds 1.8.2403.2 and LunarG); no Microsoft asset'),
    'lib/x64/libdxcompiler.dylib': ('19ad0953f7a076378ab0e135fa3a54f6f2fb88c451120fd32af340fa7030f159',
                                    'LunarG Vulkan SDK build (embeds 1.8.2403.2 and LunarG); no Microsoft asset'),
    'bin/arm64/dxc-macos': ('dc29b620ac054946e0c4a935bddc596de7a9c3051b5afdc2462db1ce571238a3',
                            'LunarG-signed build, no version string; no Microsoft asset'),
    'lib/arm64/libdxcompiler.dylib': ('daa65fc51bee4e7ea4c71ed7ddecea8397821c0e0c9f407ef786ca3944cd90d7',
                                      'LunarG-signed build, no version string; no Microsoft asset'),
}

# The text files among them. git converts their line endings on checkout when
# core.autocrlf is set, so they are hashed with CRLF folded to LF (the values
# above are those of the LF files, as the Linux tarball ships them).
DXC_TEXT_FILES = {'inc/dxcapi.h', 'inc/dxcerrors.h', 'inc/dxcisense.h', 'inc/WinAdapter.h'}

# SHA-256 of the two Microsoft release assets the dxc files above were matched
# against, for whoever wants to repeat the comparison (downloaded 2026-09-02
# from https://github.com/microsoft/DirectXShaderCompiler/releases).
DXC_RELEASE_ASSETS = {
    'v1.7.2212/dxc_2022_12_16.zip': 'ed77c7775fcf1e117bec8b5bb4de6735af101b733d3920dda083496dceef130f',
    'v1.8.2403.2/linux_dxc_2024_03_29.x86_64.tar.gz': '26051824ec198854b41a481e7040ad295200774616d45698019a05b9f9cf32df',
    'v1.7.2308/dxc_2023_08_14.zip': '01d4c4dfa37dee21afe70cac510d63001b6b611a128e3760f168765eead1e625',
}

# The dxil.dll in dxc-bin is the v1.7.2212 validator, and that release's
# LICENSE-MS.txt is a time-limited pre-release agreement (it terminates thirty
# days after a commercial release) with no right to distribute the file. The
# port ships dxil.dll beside the executable, so it takes the file of the next
# release, v1.7.2308, whose terms carry the "Distributable Code" section (the
# same text as every later release up to v1.9.2607, checked 2026-09-02; it is
# tracked as licenses/DirectXShaderCompiler-dxil.txt). Not a newer one: the
# compiler in dxc-bin is a 1.7-series build, and the v1.8.2403.2 validator
# refused its library shaders at build time ("Container part 'Runtime Data
# (RDAT)' does not match expected for module"), while the 1.7.2308 validator
# signs everything the 1.7 compiler emits (VENDORING.md, "dxc"). The Windows
# release archive is downloaded from GitHub, checked against the SHA-256
# below, and only bin/x64/dxil.dll is taken from it.
DXIL_OVERLAY = dict(
    release='v1.7.2308',
    url='https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.7.2308/dxc_2023_08_14.zip',
    asset_sha256='01d4c4dfa37dee21afe70cac510d63001b6b611a128e3760f168765eead1e625',
    member='bin/x64/dxil.dll',
    dest='bin/x64/dxil.dll',
    sha256='9cccc7ef419da73fa314fdaecae831c6c20206ae70732c9093f95193378ced10',
    version='101.7.2308.12',
)

class Failure(Exception):
    pass


GIT = shutil.which('git')


def git(args, cwd=REPO, check=True):
    """Run git with the options every call here wants; return the CompletedProcess."""
    cmd = [GIT, '-c', 'advice.detachedHead=false', '-c', 'core.longpaths=true',
           '-c', 'protocol.version=2'] + list(args)
    p = subprocess.run(cmd, cwd=str(cwd), capture_output=True, text=True, encoding='utf-8', errors='replace')
    if check and p.returncode != 0:
        raise Failure('git %s failed with %d:\n%s' % (' '.join(args), p.returncode, p.stderr.strip()))
    return p


def rmtree_force(path):
    """shutil.rmtree that copes with git's read-only object files on Windows."""
    def on_error(func, p, exc):
        os.chmod(p, stat.S_IWRITE)
        func(p)
    shutil.rmtree(path, onerror=on_error)


def sha256_of(path, text=False):
    """SHA-256 of a file; a text file is hashed with CRLF folded to LF."""
    if text:
        return hashlib.sha256(path.read_bytes().replace(b'\r\n', b'\n')).hexdigest()
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def submodule_status():
    """(state, commit, path, describe) for every submodule, recursively.
    state is ' ' (at its commit), '-' (not checked out), '+' (another commit)
    or 'U' (merge conflict), as `git submodule status` prints it."""
    out = git(['submodule', 'status', '--recursive']).stdout
    rows = []
    for line in out.splitlines():
        if not line.strip():
            continue
        state, rest = line[0], line[1:].split()
        rows.append((state, rest[0], rest[1], ' '.join(rest[2:])))
    return rows


def update_submodules(full):
    """git submodule update --init --recursive, one commit deep unless full."""
    git(['submodule', 'sync', '--recursive'])
    t0 = time.time()
    # As stored: no CRLF conversion in the trees checked out here, whatever
    # the machine's git says (Git for Windows' own config turns it on).
    args = ['-c', 'core.autocrlf=false', 'submodule', 'update', '--init', '--recursive', '--jobs', '8']
    if not full:
        args += ['--depth', '1']
    p = git(args, check=False)
    if p.returncode != 0 and not full:
        # A server that will not hand out one commit by its hash: fetch whole histories.
        print('   one-commit fetch refused (%s); fetching whole histories' % p.stderr.strip().splitlines()[-1])
        git(['-c', 'core.autocrlf=false', 'submodule', 'update', '--init', '--recursive', '--jobs', '8'])
    elif p.returncode != 0:
        raise Failure('git submodule update failed:\n%s' % p.stderr.strip())
    return time.time() - t0


def allow_long_paths(rows):
    """On Windows, core.longpaths in the checkout and every submodule. The
    nested repositories' object stores and RT64's deepest test files pass 260
    characters from a deep enough folder; this script's own git calls carry the
    setting, and writing it into each repository lets every later git command
    (a plain `git status` in the checkout) open them too."""
    if sys.platform != 'win32':
        return
    for where in [REPO] + [REPO / path for state, sha, path, describe in rows]:
        if git(['config', '--local', '--get', 'core.longpaths'], where, check=False).stdout.strip() != 'true':
            git(['config', 'core.longpaths', 'true'], where)


def keep_line_endings(rows, fresh):
    """Every submodule keeps its files as its repository stores them (LF), on
    every machine, so a build reads the same bytes on Windows as on GitHub's
    Linux and Mac runners, and license texts ship as their authors wrote
    them. The setting is written into each submodule, so later checkouts in
    it keep the rule. A checkout this run made is already as stored (the
    update above carried the setting); one made before the rule (CRLF) is
    written again, but only when it holds no change of its own."""
    rewritten, skipped = [], []
    for state, sha, path, describe in rows:
        where = REPO / path
        if git(['config', '--local', '--get', 'core.autocrlf'], where, check=False).stdout.strip() == 'false':
            continue
        if path in fresh:
            git(['config', 'core.autocrlf', 'false'], where)
            continue
        changes = [l for l in git(['status', '--porcelain', '--ignore-submodules=all'], where).stdout.splitlines()
                   if l.strip() and not l[3:].strip().endswith(DXIL_OVERLAY['dest'])]
        if changes:
            skipped.append('%s (%d local changes)' % (path, len(changes)))
            continue
        git(['config', 'core.autocrlf', 'false'], where)
        git(['rm', '--cached', '-r', '-q', '--ignore-unmatch', '.'], where)
        git(['reset', '-q', '--hard'], where)
        rewritten.append(path)
    if rewritten:
        print('   LF       %d checkouts written as their repositories store them' % len(rewritten))
    for s in skipped:
        print('   WARNING  %s: left as it is; commit or stash, then run this again' % s)


FORK_BRANCH = 'snap64'


def fetch_fork_branches(rows):
    """A one-commit checkout of a fork has the commit and no branch name:
    `git switch snap64` in it fails. Fetch the fork's branch ref for every
    submodule on one of the project's forks (a github.com/JackandBeans URL),
    so the branch can be switched to, and say when the recorded commit is not
    that branch's head (a newer fork commit not yet recorded here, or the
    reverse)."""
    behind = []
    for state, sha, path, describe in rows:
        where = REPO / path
        url = git(['config', '--local', '--get', 'remote.origin.url'], where, check=False).stdout.strip()
        if '/JackandBeans/' not in url:
            continue
        # A one-commit clone's fetch refspec names only the default branch, and
        # `git switch <name>` finds a remote branch through that refspec, so a
        # fetched ref alone is not enough: the refspec covers every branch.
        full = '+refs/heads/*:refs/remotes/origin/*'
        if git(['config', '--local', '--get-all', 'remote.origin.fetch'], where, check=False).stdout.split() != [full]:
            git(['config', 'remote.origin.fetch', full], where)
        args = ['fetch', '-q', 'origin', '+refs/heads/%s:refs/remotes/origin/%s' % (FORK_BRANCH, FORK_BRANCH)]
        if git(['rev-parse', '--is-shallow-repository'], where).stdout.strip() == 'true':
            args.insert(2, '--depth=1')
        git(args, where)
        head = git(['rev-parse', 'refs/remotes/origin/' + FORK_BRANCH], where).stdout.strip()
        if head != sha:
            behind.append("%s: %s is recorded, the fork branch %s is at %s" % (path, sha[:7], FORK_BRANCH, head[:7]))
    for b in behind:
        print('   NOTE     ' + b)


def ensure_dxil():
    """Put the DXIL_OVERLAY validator in place of the one dxc-bin carries."""
    ov = DXIL_OVERLAY
    dest = DXC / ov['dest']
    if dest.is_file() and sha256_of(dest) == ov['sha256']:
        print('   OK       %s is the %s file (%s)' % (ov['dest'], ov['release'], ov['version']))
        return
    print('   DOWNLOAD %s' % ov['url'])
    t0 = time.time()
    tmp = pathlib.Path(tempfile.mkdtemp(prefix='fetch_deps_dxc_'))
    try:
        zpath = tmp / 'asset.zip'
        try:
            with urllib.request.urlopen(ov['url'], timeout=120) as r, open(zpath, 'wb') as f:
                shutil.copyfileobj(r, f)
        except OSError as e:
            raise Failure('dxc: could not download %s: %s' % (ov['url'], e))
        actual = sha256_of(zpath)
        if actual != ov['asset_sha256']:
            raise Failure('dxc: %s hashes to %s, expected %s; not using it' % (ov['url'], actual, ov['asset_sha256']))
        with zipfile.ZipFile(zpath) as z:
            data = z.read(ov['member'])
        digest = hashlib.sha256(data).hexdigest()
        if digest != ov['sha256']:
            raise Failure('dxc: %s inside the asset hashes to %s, expected %s' % (ov['member'], digest, ov['sha256']))
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
    finally:
        rmtree_force(tmp)
    print('   REPLACED %s with the %s file, %s (%.1f s)' % (ov['dest'], ov['release'], ov['version'], time.time() - t0))


def verify_dxc():
    """Compare every file of the dxc checkout with DXC_FILES."""
    bad = []
    present = set()
    for rel, (digest, origin) in sorted(DXC_FILES.items()):
        f = DXC / rel
        if not f.is_file():
            bad.append('%s: missing' % rel)
            continue
        actual = sha256_of(f, text=rel in DXC_TEXT_FILES)
        if actual != digest:
            bad.append('%s: sha256 %s, expected %s' % (rel, actual, digest))
        present.add(rel)
    extra = sorted(str(p.relative_to(DXC)).replace('\\', '/') for p in DXC.rglob('*')
                   if p.is_file() and '.git' not in p.relative_to(DXC).parts and
                   str(p.relative_to(DXC)).replace('\\', '/') not in present)
    if extra:
        bad.append('unexpected files: ' + ', '.join(extra))
    if bad:
        raise Failure('dxc: the checkout does not match DXC_FILES:\n   ' + '\n   '.join(bad))
    print('   VERIFIED %d files by SHA-256; the Windows compiler binaries are:' % len(DXC_FILES))
    for rel in ('bin/x64/dxc.exe', 'bin/x64/dxcompiler.dll', 'bin/x64/dxil.dll'):
        print('      %-24s %s' % (rel, DXC_FILES[rel][1]))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--list', action='store_true', help="print every submodule's commit and exit")
    ap.add_argument('--full', action='store_true', help='fetch whole histories, not one commit each')
    args = ap.parse_args()
    if GIT is None:
        print('fetch_deps: git was not found on PATH', file=sys.stderr)
        return 1
    if not (REPO / '.git').exists():
        print('fetch_deps: %s is not a git checkout; clone the repository (a source archive has no submodules)' % REPO,
              file=sys.stderr)
        return 1
    try:
        if args.list:
            for state, sha, path, describe in submodule_status():
                print('%s %s  %-62s %s' % (state, sha, path, describe))
            print('  (then %s replaced by the %s file, sha256 %s)' % (
                'lib/rt64/src/contrib/dxc/' + DXIL_OVERLAY['dest'], DXIL_OVERLAY['release'], DXIL_OVERLAY['sha256'][:16]))
            return 0
        print('repository: %s' % REPO)
        before = {path for state, sha, path, d in submodule_status() if state == ' '}
        seconds = update_submodules(args.full)
        rows = submodule_status()
        allow_long_paths(rows)
        keep_line_endings(rows, {path for state, sha, path, d in rows if path not in before})
        fetch_fork_branches(rows)
        rows = submodule_status()
        wrong = [(s, p) for s, sha, p, d in rows if s != ' ']
        if wrong:
            raise Failure('not at their recorded commits:\n   ' + '\n   '.join('%s %s' % w for w in wrong))
        fetched = len([r for r in rows if r[2] not in before])
        print('== submodules: %d at their recorded commits (%d fetched now, %.0f s)' % (len(rows), fetched, seconds))
        print('== dxc  lib/rt64/src/contrib/dxc')
        ensure_dxil()
        verify_dxc()
    except Failure as e:
        print('\nFAILED: %s' % e, file=sys.stderr)
        return 1
    print('\ndone')
    return 0


if __name__ == '__main__':
    sys.exit(main())
