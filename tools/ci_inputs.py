#!/usr/bin/env python3
"""The private inputs of the build workflows, gathered into one folder, and
the check that they belong to the tree being built.

    python tools/ci_inputs.py <folder> [--mod <snap64_zz_hooktest.nrm>]
    python tools/ci_inputs.py --check <folder>

GitHub's machines build from this repository, which may hold nothing made
from the ROM; `.github/workflows/build.yml` (Windows and Linux) and
`macos.yml` clone the rest from the private repository
JackandBeans/Snap64RecompInputs. The first form lays out a working copy of
that repository (a clone, or an empty folder to become one) from this
checkout:

    RecompiledFuncs/             the recompiled game (BUILDING.md, step 4)
    RecompiledPatches/           the recompiled patches (step 8)
    patches/build/patches.bin    the patches' data (step 8)
    pokemonsnap.z64              the ROM (step 3); the build recompiles the
                                 audio microcode from it, the checks boot it
    saves/pokemonsnap.bin        the save the scoring replay was recorded on
                                 (build-win/Release/saves)
    mods/snap64_zz_hooktest.nrm  a mod that hooks three game functions, for
                                 the Mac's mod check (Snap64RecompMods' zz_hooktest)
    inputs.json                  the fingerprint of the tree they were made from

The fingerprint is a SHA-256 over the git blobs of every tracked file the
recompiled code is made from: `pokemonsnap.us.toml`, `patches.toml` and
everything under `patches/`. Each workflow runs the second form on its clone
before building, and stops when the fingerprint is not the tree's own: the
inputs were made from another tree, and the build would carry its patches.

Laying out refuses a checkout whose inputs may be stale: a local change to
one of those files, or a patch source newer than the recompiled patches
(every patch build rewrites all of them). A checkout that switched branches
and back rewrites the files it touched, so they look newer even when their
content is back; rebuild the patches (step 8, a minute) and lay out again.
The recompiled game cannot be dated that way (the recompiler and
tools/hook_funcs.py rewrite only some of its files): after a change to
`pokemonsnap.us.toml`, recompile the game (step 4) before laying out. The workflows run tools/hook_funcs.py
over the recompiled game themselves, so the state it is copied in does not
matter. Nothing here is committed to this repository; the folder is pushed
to the private one.
"""
import argparse
import datetime
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_MOD = pathlib.Path.home() / 'Snap64RecompMods' / 'build' / 'zz_hooktest' / 'snap64_zz_hooktest.nrm'
FINGERPRINT_PATHS = ['pokemonsnap.us.toml', 'patches.toml', 'patches']
MANIFEST = 'inputs.json'
README = """# Snap64RecompInputs

Private. What the build workflows of JackandBeans/Snap64Recomp need and no
public tree may hold: the recompiled game and patches, the ROM, the save the
scoring replay was recorded on, and a hook test mod. `inputs.json` names the
tree they were made from. Laid out by `tools/ci_inputs.py` in that
repository; read by `.github/workflows/build.yml` and `macos.yml` with the
SNAP64_INPUTS_TOKEN secret.
"""


def git(*args):
    return subprocess.run(['git', '-c', 'core.longpaths=true'] + list(args), cwd=str(ROOT),
                          capture_output=True, text=True, encoding='utf-8', check=True).stdout


def tree_fingerprint():
    """(sha256 hex, file count) over the tracked files the recompiled code is made from."""
    lines = []
    for row in git('ls-tree', '-r', 'HEAD', '--', *FINGERPRINT_PATHS).splitlines():
        meta, path = row.split('\t', 1)
        lines.append('%s %s\n' % (meta.split()[2], path))
    lines.sort()
    return hashlib.sha256(''.join(lines).encode('utf-8')).hexdigest(), len(lines)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def staleness():
    """Reasons the recompiled output in this checkout may not come from its tracked files."""
    reasons = []
    changed = [l[3:] for l in git('status', '--porcelain', '--', *FINGERPRINT_PATHS).splitlines() if l.strip()]
    if changed:
        reasons.append('local changes to %s' % ', '.join(changed))
    tracked = [ROOT / p for p in git('ls-files', '--', *FINGERPRINT_PATHS).splitlines()]
    patch_sources = [p for p in tracked if p.name != 'pokemonsnap.us.toml']
    patch_outputs = [ROOT / 'patches' / 'build' / 'patches.bin'] + sorted((ROOT / 'RecompiledPatches').glob('*'))
    if patch_outputs[1:] and all(p.exists() for p in patch_outputs):
        built = min(p.stat().st_mtime for p in patch_outputs)
        newer = [p.relative_to(ROOT).as_posix() for p in patch_sources if p.stat().st_mtime > built]
        if newer:
            reasons.append('newer than the recompiled patches: %s (rebuild them, BUILDING.md step 8)' % ', '.join(newer))
    return reasons


def check(folder):
    manifest = pathlib.Path(folder) / MANIFEST
    mine, count = tree_fingerprint()
    if not manifest.is_file():
        print('ci_inputs: %s has no %s; lay the inputs out again with tools/ci_inputs.py from the tree being built' % (folder, MANIFEST))
        return 1
    theirs = json.loads(manifest.read_text(encoding='utf-8'))
    print('this tree: fingerprint %s over %d files' % (mine[:16], count))
    print('the inputs: fingerprint %s, made from %s on %s' % (theirs.get('fingerprint', '?')[:16], theirs.get('commit', '?')[:12], theirs.get('made', '?')))
    if theirs.get('fingerprint') != mine:
        print('ci_inputs: the inputs were made from another tree (its patches or recompiler configs differ from '
              'this one\'s). Rebuild the patches on this tree, lay the inputs out with tools/ci_inputs.py and push '
              'the private repository (BUILDING.md, step 15).')
        return 1
    print('the inputs belong to this tree')
    return 0


def layout(dest, mod):
    reasons = staleness()
    if reasons:
        print('ci_inputs: the recompiled code in this checkout may not be its own:')
        for r in reasons:
            print('   ' + r)
        return 1
    dest.mkdir(parents=True, exist_ok=True)
    sources = {
        'RecompiledFuncs': ROOT / 'RecompiledFuncs',
        'RecompiledPatches': ROOT / 'RecompiledPatches',
        'patches/build/patches.bin': ROOT / 'patches' / 'build' / 'patches.bin',
        'pokemonsnap.z64': ROOT / 'pokemonsnap.z64',
        'saves/pokemonsnap.bin': ROOT / 'build-win' / 'Release' / 'saves' / 'pokemonsnap.bin',
        'mods/snap64_zz_hooktest.nrm': pathlib.Path(mod),
    }
    missing = [name for name, src in sources.items() if not src.exists()]
    if missing:
        print('missing: %s' % ', '.join('%s (%s)' % (m, sources[m]) for m in missing))
        return 1
    total = 0
    for name, src in sources.items():
        target = dest / name
        if target.exists():
            if target.is_dir():
                shutil.rmtree(target)
            else:
                target.unlink()
        target.parent.mkdir(parents=True, exist_ok=True)
        if src.is_dir():
            shutil.copytree(src, target)
            files = [f for f in target.rglob('*') if f.is_file()]
            size = sum(f.stat().st_size for f in files)
            print('%-28s %4d files, %6.1f MB' % (name + '/', len(files), size / 1e6))
        else:
            shutil.copyfile(src, target)
            size = target.stat().st_size
            print('%-28s %11.1f MB  sha256 %s' % (name, size / 1e6, sha256(target)[:16]))
        total += size
    fingerprint, count = tree_fingerprint()
    manifest = {
        'fingerprint': fingerprint,
        'files': count,
        'commit': git('rev-parse', 'HEAD').strip(),
        'made': datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'),
    }
    (dest / MANIFEST).write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8', newline='\n')
    (dest / 'README.md').write_text(README, encoding='utf-8', newline='\n')
    print('%-28s fingerprint %s over %d files, from %s' % (MANIFEST, fingerprint[:16], count, manifest['commit'][:12]))
    print('%.1f MB in %s' % (total / 1e6, dest))
    return 0


def main():
    ap = argparse.ArgumentParser(description='The build workflows\' private inputs: lay them out, or check a clone of them.')
    ap.add_argument('folder', help='the working copy of the private repository (or, with --check, a clone of it)')
    ap.add_argument('--check', action='store_true', help='check that the inputs in the folder were made from this tree')
    ap.add_argument('--mod', default=str(DEFAULT_MOD), help='the hook test mod (default: %(default)s)')
    args = ap.parse_args()
    if args.check:
        return check(args.folder)
    return layout(pathlib.Path(args.folder).resolve(), args.mod)


if __name__ == '__main__':
    sys.exit(main())
