#!/usr/bin/env python3
"""Fetch the vendored trees a clean checkout of Snap64 Recomp does not carry.

A `git clone` of this repository has no `lib/SDL`, no `lib/DirectX-Headers`
and, under `lib/rt64/src/contrib`, only the port's three plume files. This script
puts everything else there at the exact upstream commits the port was built
against (VENDORING.md, "Recovered pins"), verifies it, and leaves the port's
own changes to plume in place. It is idempotent: a tree already at its pin is
left alone, and running the script twice does nothing the second time.

    python tools/fetch_deps.py            # fetch what is missing
    python tools/fetch_deps.py --list     # print the pin table and exit
    python tools/fetch_deps.py --dry-run  # say what would be done
    python tools/fetch_deps.py --only SDL --only zstd

Needs Python 3 (standard library only), git on PATH and network access to
github.com. Nothing here is a submodule: each tree is fetched by commit into a
detached checkout of its own (`git init`, `git fetch --depth 1 <url> <sha>`,
`git checkout --detach <sha>`), so no history is downloaded and no branch or
tag has to survive upstream; only the commit does.

What each entry is and how its pin was established is in VENDORING.md. The
short version of the table below: `confidence` is `exact` when the developer's
tree was byte-identical to the pinned commit (file modes and symlinks aside,
which NTFS does not keep), `high` when it matched apart from files the port
changes on purpose or files nothing compiles.

Two entries are not plain checkouts:

* `dxc` is rt64's `dxc-bin` repository, whose 17 files are compiler binaries
  and headers. After the checkout every file's SHA-256 is compared with the
  table in `DXC_FILES`, which also records where each binary came from
  (a Microsoft DirectXShaderCompiler release asset, or a private build that
  matches no release; see VENDORING.md). A mismatch stops the script. One
  file is not dxc-bin's: `bin/x64/dxil.dll` is replaced by the file of
  Microsoft's release v1.7.2308, downloaded from the release asset and
  checked by SHA-256 (`DXIL_OVERLAY` below says why).
* `plume` carries three files of the port's own, `plume_d3d12.cpp`,
  `plume_d3d12.h` and `plume_render_interface.h`, all tracked by this
  repository. The clone writes upstream's versions over them, so they are put
  back with `git checkout --` afterwards; an uncommitted local edit to one of
  them is kept instead, and said so.

The five directories rt64 keeps as plain files in its own tree (`json`,
`miniz`, `plainargs`, `project64`, `utf8conv`) are taken from rt64's
repository at the commit the port's `lib/rt64` is forked from and verified by
git blob hash.
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

CONTRIB = 'lib/rt64/src/contrib'

# The commit of rt64/rt64 that lib/rt64 (outside contrib) is forked from. Its
# .gitmodules is what the contrib pins below were read from, and its plain
# contrib subtrees are copied from it (RT64_PLAIN_FILES).
RT64_UPSTREAM = 'https://github.com/rt64/rt64.git'
RT64_BASE = 'a012a2301908b130f9251dd3ec0aaeebf9678d80'  # 2026-07-22, "Improve synchronization detection for tiles being sampled. (#254)"

# One entry per git checkout, parents before the trees nested inside them.
# path is relative to the repository root, forward slashes.
GIT_PINS = [
    dict(name='SDL', path='lib/SDL',
         url='https://github.com/libsdl-org/SDL.git',
         sha='fa24d868ac2f8fd558e4e914c9863411245db8fd',
         describe='release-2.30.11', confidence='exact'),
    dict(name='DirectX-Headers', path='lib/DirectX-Headers',
         url='https://github.com/microsoft/DirectX-Headers.git',
         sha='ee479f0bd5f7b884f202bcf0c3f076cc050dd256',
         describe='v1.619.5', confidence='exact'),
    # RT64's submodules, at the gitlinks recorded by rt64/rt64 RT64_BASE.
    dict(name='ddspp', path=CONTRIB + '/ddspp',
         url='https://github.com/redorav/ddspp.git',
         sha='21ca0c4319dfd5a161c5f2a0c406e8f60194ea6c',
         describe='tag 1.11, 2024-08-07', confidence='exact'),
    dict(name='dxc', path=CONTRIB + '/dxc',
         url='https://github.com/rt64/dxc-bin',
         sha='cc15e715ee378a4f675b335bd1071ff105873fc8',
         describe='dxc-bin 2024-05-16 "Add x64/macos v1.8.2403.2"', confidence='exact',
         verify_sha256='DXC_FILES'),
    dict(name='hlslpp', path=CONTRIB + '/hlslpp',
         url='https://github.com/redorav/hlslpp',
         sha='6f5274c66132e8f951c400103d897582b8f21491',
         describe='tag 3.6, 2024-12-22', confidence='exact'),
    dict(name='im3d', path=CONTRIB + '/im3d',
         url='https://github.com/john-chapman/im3d',
         sha='d03941725fd0bd08c78c46e3e5b0265526e9d060',
         describe='2023-01-09 "Add Draw Cone. (#60)"', confidence='exact'),
    dict(name='imgui', path=CONTRIB + '/imgui',
         url='https://github.com/ocornut/imgui',
         sha='277ae93c41314ba5f4c7444f37c4319cdf07e8cf',
         describe='tag v1.90.4, 2024-02-22', confidence='exact'),
    dict(name='implot', path=CONTRIB + '/implot',
         url='https://github.com/epezent/implot',
         sha='f156599faefe316f7dd20fe6c783bf87c8bb6fd9',
         describe='v0.16-14-gf156599, 2024-01-22', confidence='exact'),
    dict(name='mupen64plus-core', path=CONTRIB + '/mupen64plus-core',
         url='https://github.com/mupen64plus/mupen64plus-core',
         sha='860fac3fbae94194a392c1d9857e185eda6d083e',
         describe='2.5.9-484-g860fac3, 2024-01-24', confidence='exact'),
    dict(name='mupen64plus-win32-deps', path=CONTRIB + '/mupen64plus-win32-deps',
         url='https://github.com/mupen64plus/mupen64plus-win32-deps',
         sha='de8111fdcb89144abc16c85650ce4e21e028bfb5',
         describe='2.5-21-gde8111f, 2023-03-02 (104 MB of prebuilt libraries)', confidence='exact'),
    dict(name='nativefiledialog-extended', path=CONTRIB + '/nativefiledialog-extended',
         url='https://github.com/btzy/nativefiledialog-extended',
         sha='17b6e8ce219c0677f94b63636abb9296b28841ca',
         describe='v1.1.1-6-g17b6e8c, 2024-02-24', confidence='exact'),
    dict(name='plume', path=CONTRIB + '/plume',
         url='https://github.com/renderbag/plume.git',
         sha='51b1ad443b9f202c5cfc930ae25345d3f2ba7716',
         describe='2026-01-28 "Force residency sets to off.", branch metal-release-pool-refactor-plus-sets-off',
         confidence='high',
         why=('863 of 866 files match this commit; the other three are the port\'s own '
              '(plume_d3d12.cpp, plume_d3d12.h, plume_render_interface.h) and are put back below. '
              'The commit is not on plume\'s main branch.'),
         port_files=['plume_d3d12.cpp', 'plume_d3d12.h', 'plume_render_interface.h']),
    # plume's own submodules, at the gitlinks recorded by the plume commit above.
    dict(name='plume/D3D12MemoryAllocator', path=CONTRIB + '/plume/contrib/D3D12MemoryAllocator',
         url='https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator',
         sha='9ef66bc14edd10dee0de3a545b98578363552f66',
         describe='v3.0.1', confidence='exact'),
    dict(name='plume/Vulkan-Headers', path=CONTRIB + '/plume/contrib/Vulkan-Headers',
         url='https://github.com/KhronosGroup/Vulkan-Headers',
         sha='2fa203425eb4af9dfc6b03f97ef72b0b5bcb8350',
         describe='v1.4.335', confidence='exact'),
    dict(name='plume/VulkanMemoryAllocator', path=CONTRIB + '/plume/contrib/VulkanMemoryAllocator',
         url='https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator',
         sha='29b35ea4232688c0f42cdff0c10848290760a417',
         describe='v3.2.1-5-g29b35ea', confidence='exact'),
    dict(name='plume/volk', path=CONTRIB + '/plume/contrib/volk',
         url='https://github.com/zeux/volk',
         sha='be3dbd49bf77052665e96b6c7484af855e7e5f67',
         describe='vulkan-sdk-1.4.321.0-7-gbe3dbd4', confidence='exact'),
    dict(name='re-spirv', path=CONTRIB + '/re-spirv',
         url='https://github.com/rt64/re-spirv',
         sha='5d6b756ee62760f71b65d37e41a0b5a3dab90507',
         describe='2025-05-03 "Missing cstd include."', confidence='exact'),
    # re-spirv's submodule, at the gitlink recorded by the re-spirv commit above.
    dict(name='re-spirv/SPIRV-Headers', path=CONTRIB + '/re-spirv/external/SPIRV-Headers',
         url='https://github.com/KhronosGroup/SPIRV-Headers',
         sha='f013f08e4455bcc1f0eed8e3dd5e2009682656d9',
         describe='vulkan-sdk-1.3.290.0-5-gf013f08, 2024-07-29', confidence='exact'),
    dict(name='spirv-cross', path=CONTRIB + '/spirv-cross',
         url='https://github.com/KhronosGroup/SPIRV-Cross.git',
         sha='6173e24b31f09a0c3217103a130e74c4ddec14a6',
         describe='vulkan-sdk-1.4.304.0-2-g6173e24b, 2024-12-13', confidence='exact'),
    dict(name='stb', path=CONTRIB + '/stb',
         url='https://github.com/nothings/stb',
         sha='ae721c50eaf761660b4f90cc590453cdb0c2acd0',
         describe='2024-02-12', confidence='exact'),
    dict(name='xxHash', path=CONTRIB + '/xxHash',
         url='https://github.com/Cyan4973/xxHash',
         sha='1864a50c9b5cf8500d8e9e61ed92aa0dd3772750',
         describe='dev branch, 2024-02-12 (v0.7.4-707-g1864a50)', confidence='exact'),
    dict(name='zstd', path=CONTRIB + '/zstd',
         url='https://github.com/facebook/zstd',
         sha='0ff651dd876823b99fa5c5f53292be28381aee9b',
         describe='dev branch, 2024-07-16 (merge of PR #4096)', confidence='high',
         why=('636 of 638 files match; tests/cli-tests/bin/unzstd and zstdcat are symlinks '
              'upstream and were empty files in the developer\'s tree. Nothing compiled differs.')),
]

# Files rt64/rt64 keeps as plain files under src/contrib (not submodules), with
# their git blob hashes at RT64_BASE. Copied from a temporary checkout of that
# commit and verified with `git hash-object`, which applies the same line-ending
# normalisation as a checkout, so the check holds with or without autocrlf.
RT64_PLAIN_FILES = {
    'src/contrib/json/json.hpp': '82d69f7c5d044c9887c96b90c97f5639083ecd14',
    'src/contrib/miniz/miniz.c': 'b37a067ccee4c1ea297d7bbfb009713944767042',
    'src/contrib/miniz/miniz.h': 'f54d01be956eb1d2a8d766ca8f9ceb8300621668',
    'src/contrib/plainargs/plainargs.h': '18ee421a12cbccf34b5bcbd9159b8568ce41ff75',
    'src/contrib/project64/Base.h': 'edd242e1b1bc44977633eeff7d1c3c8317f937e5',
    'src/contrib/project64/Video.h': '32f3cdf9e4b66588d782ad6b7dc888e47422ec84',
    'src/contrib/utf8conv/utf8conv.h': 'a541b328f8b9ee603aec431b44a6e65f39d44f82',
    'src/contrib/utf8conv/utf8except.h': '7b2533098e698fe984b26eb9357bfe823faf5904',
}

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


def git(args, cwd, check=True):
    """Run git with the options every call here wants; return the CompletedProcess."""
    cmd = [GIT, '-c', 'advice.detachedHead=false', '-c', 'core.longpaths=true'] + list(args)
    p = subprocess.run(cmd, cwd=str(cwd), capture_output=True, text=True, encoding='utf-8', errors='replace')
    if check and p.returncode != 0:
        raise Failure('git %s (in %s) failed with %d:\n%s' % (' '.join(args), cwd, p.returncode, p.stderr.strip()))
    return p


def rmtree_force(path):
    """shutil.rmtree that copes with git's read-only object files on Windows."""
    def on_error(func, p, exc):
        os.chmod(p, stat.S_IWRITE)
        func(p)
    shutil.rmtree(path, onerror=on_error)


def repo_tracked(rel):
    """Is rel (repo-relative, forward slashes) tracked by this repository?"""
    return git(['ls-files', '--error-unmatch', '--', rel], REPO, check=False).returncode == 0


def checkout_state(path):
    """Classify what is at path: absent | empty | repo:<sha> | stale | nogit."""
    if not path.exists():
        return 'absent', None

    def head():
        # None when the repository has no commit yet (an earlier fetch failed).
        p = git(['rev-parse', '--verify', '-q', 'HEAD^{commit}'], path, check=False)
        return p.stdout.strip() or None

    dotgit = path / '.git'
    if dotgit.is_dir():
        return 'repo', head()
    if dotgit.is_file():
        line = dotgit.read_text(encoding='utf-8', errors='replace').strip()
        gitdir = line[len('gitdir:'):].strip() if line.startswith('gitdir:') else ''
        target = (path / gitdir) if gitdir and not os.path.isabs(gitdir) else pathlib.Path(gitdir)
        if gitdir and target.is_dir():
            return 'repo', head()
        return 'stale', gitdir
    if any(path.iterdir()):
        return 'nogit', None
    return 'empty', None


def fetch_pin(entry, path, dry_run):
    """Bring path to entry['sha'] with a depth-1 fetch of that commit."""
    if dry_run:
        print('   would fetch %s and check it out' % entry['sha'])
        return
    path.mkdir(parents=True, exist_ok=True)
    if not (path / '.git').exists():
        git(['init', '-q'], path)
    t0 = time.time()
    p = git(['fetch', '-q', '--depth', '1', entry['url'], entry['sha']], path, check=False)
    if p.returncode != 0:
        raise Failure('%s: could not fetch %s from %s:\n%s' % (entry['name'], entry['sha'], entry['url'], p.stderr.strip()))
    # -f: the checkout may have to write over files that were already in the
    # directory (a fresh clone of this repository has plume/plume_d3d12.cpp
    # there before plume is fetched); the port's files are put back afterwards.
    git(['checkout', '-q', '-f', '--detach', entry['sha']], path)
    head = git(['rev-parse', '--verify', 'HEAD^{commit}'], path).stdout.strip()
    if head != entry['sha']:
        raise Failure('%s: HEAD is %s after checkout, expected %s' % (entry['name'], head, entry['sha']))
    print('   FETCHED  depth-1 fetch of the pin, checked out detached (%.1f s)' % (time.time() - t0))


def modified_files(path, ignore):
    """Tracked files of the checkout at path that differ from its HEAD."""
    out = git(['status', '--porcelain', '--untracked-files=no'], path).stdout
    names = [line[3:].strip().strip('"') for line in out.splitlines() if line.strip()]
    return [n for n in names if n not in ignore]


def sha256_of(path, text=False):
    """SHA-256 of a file; a text file is hashed with CRLF folded to LF."""
    if text:
        return hashlib.sha256(path.read_bytes().replace(b'\r\n', b'\n')).hexdigest()
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def ensure_dxil(path, dry_run):
    """Put the DXIL_OVERLAY validator in place of the one dxc-bin carries."""
    ov = DXIL_OVERLAY
    dest = path / ov['dest']
    if dest.is_file() and sha256_of(dest) == ov['sha256']:
        print('   OK       %s is the %s file (%s)' % (ov['dest'], ov['release'], ov['version']))
        return True
    if dry_run:
        print('   would download %s and take %s from it' % (ov['url'], ov['member']))
        return False
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
    return True


def verify_dxc(path):
    """Compare every file of the dxc checkout with DXC_FILES."""
    bad = []
    present = set()
    for rel, (digest, origin) in sorted(DXC_FILES.items()):
        f = path / rel
        if not f.is_file():
            bad.append('%s: missing' % rel)
            continue
        actual = sha256_of(f, text=rel in DXC_TEXT_FILES)
        if actual != digest:
            bad.append('%s: sha256 %s, expected %s' % (rel, actual, digest))
        present.add(rel)
    extra = sorted(str(p.relative_to(path)).replace('\\', '/') for p in path.rglob('*')
                   if p.is_file() and '.git' not in p.relative_to(path).parts and
                   str(p.relative_to(path)).replace('\\', '/') not in present)
    if extra:
        bad.append('unexpected files: ' + ', '.join(extra))
    if bad:
        raise Failure('dxc: the checkout does not match DXC_FILES:\n   ' + '\n   '.join(bad))
    print('   VERIFIED %d files by SHA-256; the Windows compiler binaries are:' % len(DXC_FILES))
    for rel in ('bin/x64/dxc.exe', 'bin/x64/dxcompiler.dll', 'bin/x64/dxil.dll'):
        print('      %-24s %s' % (rel, DXC_FILES[rel][1]))


def restore_port_files(entry, path, kept, fetched):
    """Put the port's own plume files back after (or check them after skipping) a checkout."""
    rel_dir = entry['path']
    warned = False
    for name in entry.get('port_files', []):
        rel = rel_dir + '/' + name
        f = path / name
        tracked = repo_tracked(rel)
        if fetched:
            if name in kept:
                f.write_bytes(kept[name])
            if tracked:
                clean = git(['diff', '--quiet', 'HEAD', '--', rel], REPO, check=False).returncode == 0
                if clean or name not in kept:
                    git(['checkout', '--', rel], REPO)
                    print('   RESTORED %s from this repository (git checkout -- %s)' % (name, rel))
                else:
                    print('   KEPT     %s: your uncommitted local version, not the committed one' % name)
            elif name in kept:
                print('   KEPT     %s (not tracked; the copy that was there before the fetch)' % name)
        elif tracked:
            if git(['diff', '--quiet', 'HEAD', '--', rel], REPO, check=False).returncode != 0:
                print('   WARNING  %s differs from the committed version; left alone'
                      ' (git checkout -- %s restores it)' % (name, rel))
                warned = True
    return warned


def process_git_pin(entry, args):
    path = REPO / entry['path']
    tag = 'exact' if entry['confidence'] == 'exact' else 'NOT EXACT: %s' % entry['confidence']
    print('== %-28s %s' % (entry['name'], entry['path']))
    print('   pin %s  %s  [%s]' % (entry['sha'], entry.get('describe', ''), tag))
    if entry['confidence'] != 'exact':
        print('   NOTE     ' + entry['why'])
    state, detail = checkout_state(path)
    port_files = entry.get('port_files', [])
    # Files allowed to differ from the checkout's HEAD: the port's own plume
    # files, and the validator DXIL_OVERLAY puts in place.
    ignore = set(port_files)
    if entry.get('verify_sha256'):
        ignore.add(DXIL_OVERLAY['dest'])
    warned = False
    if state == 'repo' and detail == entry['sha']:
        mods = modified_files(path, ignore)
        if mods:
            print('   WARNING  already at the pin, but %d tracked file(s) are modified: %s'
                  % (len(mods), ', '.join(mods[:5]) + (' ...' if len(mods) > 5 else '')))
            warned = True
        else:
            print('   OK       already at the pin')
        if entry.get('verify_sha256'):
            if ensure_dxil(path, args.dry_run):
                verify_dxc(path)
        if not args.dry_run:
            warned = restore_port_files(entry, path, {}, fetched=False) or warned
        return 'ok', warned
    if state == 'stale':
        print('   UNVERIFIED  present, but its .git points at a git directory that no longer exists (%s);'
              ' the contents were not checked. Delete the directory to fetch it at the pin.' % detail)
        if entry.get('verify_sha256'):
            # The validator that ships is the one file worth putting right
            # even in a tree that is otherwise left alone.
            ensure_dxil(path, args.dry_run)
        return 'unverified', warned
    if state == 'repo' and detail is None:
        print('   initialised but empty (an earlier fetch did not finish), fetching the pin')
    elif state == 'repo':
        mods = modified_files(path, ignore)
        if mods:
            raise Failure('%s: checkout is at %s, not the pin, and has %d modified file(s) (%s); not touching it'
                          % (entry['name'], detail, len(mods), ', '.join(mods[:5])))
        print('   at %s, fetching the pin' % detail)
    elif state == 'nogit':
        extra = [str(p.relative_to(path)).replace('\\', '/') for p in path.rglob('*') if p.is_file()]
        foreign = [f for f in extra if not repo_tracked(entry['path'] + '/' + f)]
        if foreign:
            raise Failure('%s: %s exists, is not a git checkout and holds %d file(s) this repository does not track'
                          ' (%s); delete or move it to fetch the pin'
                          % (entry['name'], entry['path'], len(foreign), ', '.join(foreign[:5])))
    kept = {}
    for name in port_files:
        f = path / name
        if f.is_file():
            kept[name] = f.read_bytes()
    fetch_pin(entry, path, args.dry_run)
    if args.dry_run:
        return 'would-fetch', warned
    if entry.get('verify_sha256'):
        ensure_dxil(path, False)
        verify_dxc(path)
    warned = restore_port_files(entry, path, kept, fetched=True) or warned
    return 'fetched', warned


def blob_hash(rel):
    """git's blob hash of a working-tree file, with the repository's line-ending normalisation."""
    return git(['hash-object', '--', rel], REPO).stdout.strip()


def process_rt64_plain(args):
    print('== %-28s %s/{json,miniz,plainargs,project64,utf8conv}' % ('rt64 plain subtrees', CONTRIB))
    print('   pin %s  rt64/rt64 %s  [exact]' % (RT64_BASE, 'main, 2026-07-22'))
    missing = []
    for up_rel, blob in sorted(RT64_PLAIN_FILES.items()):
        rel = 'lib/rt64/' + up_rel
        f = REPO / rel
        if not f.is_file():
            missing.append((up_rel, 'missing'))
        elif blob_hash(rel) != blob:
            missing.append((up_rel, 'differs from the pinned blob'))
    if not missing:
        print('   OK       all %d files present and matching' % len(RT64_PLAIN_FILES))
        return 'ok'
    for up_rel, why in missing:
        print('   %s: %s' % (up_rel, why))
    if args.dry_run:
        print('   would copy them from a temporary checkout of rt64/rt64 %s' % RT64_BASE)
        return 'would-fetch'
    t0 = time.time()
    tmp = pathlib.Path(tempfile.mkdtemp(prefix='fetch_deps_rt64_'))
    try:
        git(['init', '-q'], tmp)
        p = git(['fetch', '-q', '--depth', '1', RT64_UPSTREAM, RT64_BASE], tmp, check=False)
        if p.returncode != 0:
            raise Failure('rt64: could not fetch %s from %s:\n%s' % (RT64_BASE, RT64_UPSTREAM, p.stderr.strip()))
        dirs = sorted({up_rel.rsplit('/', 1)[0] for up_rel in RT64_PLAIN_FILES})
        git(['checkout', '-q', RT64_BASE, '--'] + dirs, tmp)
        for up_rel, blob in sorted(RT64_PLAIN_FILES.items()):
            src = tmp / up_rel
            dst = REPO / 'lib/rt64' / up_rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, dst)
            actual = blob_hash('lib/rt64/' + up_rel)
            if actual != blob:
                raise Failure('rt64: %s copied from %s hashes to %s, expected %s' % (up_rel, RT64_BASE, actual, blob))
    finally:
        rmtree_force(tmp)
    print('   FETCHED  %d files copied from rt64/rt64 %s and verified by blob hash (%.1f s)'
          % (len(RT64_PLAIN_FILES), RT64_BASE[:10], time.time() - t0))
    return 'fetched'


def print_table():
    print('%-28s %-8s %-40s %s' % ('name', 'conf.', 'commit', 'path'))
    for e in GIT_PINS:
        print('%-28s %-8s %-40s %s' % (e['name'], e['confidence'], e['sha'], e['path']))
        if e['confidence'] != 'exact':
            print('%-28s %s' % ('', e['why']))
    print('%-28s %-8s %-40s %s' % ('rt64 plain subtrees', 'exact', RT64_BASE, CONTRIB + '/{json,miniz,plainargs,project64,utf8conv}'))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--list', action='store_true', help='print the pin table and exit')
    ap.add_argument('--dry-run', action='store_true', help='report what would be fetched without fetching')
    ap.add_argument('--only', action='append', default=[], metavar='NAME',
                    help='handle only this entry (repeatable; names as in --list)')
    args = ap.parse_args()
    if args.list:
        print_table()
        return 0
    if GIT is None:
        print('fetch_deps: git was not found on PATH', file=sys.stderr)
        return 1
    print('repository: %s' % REPO)
    counts = {}
    warned = []
    unverified = []
    try:
        for entry in GIT_PINS:
            if args.only and entry['name'] not in args.only:
                continue
            result, w = process_git_pin(entry, args)
            counts[result] = counts.get(result, 0) + 1
            if w:
                warned.append(entry['name'])
            if result == 'unverified':
                unverified.append(entry['name'])
        if not args.only or 'rt64' in args.only:
            result = process_rt64_plain(args)
            counts[result] = counts.get(result, 0) + 1
    except Failure as e:
        print('\nFAILED: %s' % e, file=sys.stderr)
        return 1
    print('\nsummary: ' + ', '.join('%s %d' % (k, v) for k, v in sorted(counts.items())))
    if unverified:
        print('UNVERIFIED (present, not checked against the pin): ' + ', '.join(unverified))
    if warned:
        print('WARNINGS for: ' + ', '.join(warned))
    return 0


if __name__ == '__main__':
    sys.exit(main())
