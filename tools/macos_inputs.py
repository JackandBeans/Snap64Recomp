#!/usr/bin/env python3
"""The private inputs of the macOS workflow, gathered into one folder.

    python tools/macos_inputs.py <folder> [--mod <snap64_zz_hooktest.nrm>]

GitHub's Mac builds from this repository, which may hold nothing made from
the ROM; `.github/workflows/macos.yml` clones the rest from the private
repository JackandBeans/Snap64RecompInputs. This lays out a working copy of
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
                                 the mod check (Snap64RecompMods' zz_hooktest)

The patches differ between branches, so gather them from the tree the
workflow will build, after its patches were rebuilt (step 8): for a release,
on the release branch. The workflow runs tools/hook_funcs.py over the
recompiled game itself, so the state it is copied in does not matter.
Nothing here is committed to this repository; the folder is pushed to the
private one.
"""
import argparse
import hashlib
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_MOD = pathlib.Path.home() / 'Snap64RecompMods' / 'build' / 'zz_hooktest' / 'snap64_zz_hooktest.nrm'
README = """# Snap64RecompInputs

Private. What the macOS workflow of JackandBeans/Snap64Recomp needs and no
public tree may hold: the recompiled game and patches, the ROM, the save the
scoring replay was recorded on, and a hook test mod. Laid out by
`tools/macos_inputs.py` in that repository; read by
`.github/workflows/macos.yml` with the SNAP64_INPUTS_TOKEN secret.
"""


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description='The macOS workflow\'s private inputs, gathered into one folder.')
    ap.add_argument('folder', help='the working copy of the private repository')
    ap.add_argument('--mod', default=str(DEFAULT_MOD), help='the hook test mod (default: %(default)s)')
    args = ap.parse_args()

    dest = pathlib.Path(args.folder).resolve()
    dest.mkdir(parents=True, exist_ok=True)
    sources = {
        'RecompiledFuncs': ROOT / 'RecompiledFuncs',
        'RecompiledPatches': ROOT / 'RecompiledPatches',
        'patches/build/patches.bin': ROOT / 'patches' / 'build' / 'patches.bin',
        'pokemonsnap.z64': ROOT / 'pokemonsnap.z64',
        'saves/pokemonsnap.bin': ROOT / 'build-win' / 'Release' / 'saves' / 'pokemonsnap.bin',
        'mods/snap64_zz_hooktest.nrm': pathlib.Path(args.mod),
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
    (dest / 'README.md').write_text(README, encoding='utf-8', newline='\n')
    print('%.1f MB in %s' % (total / 1e6, dest))
    return 0


if __name__ == '__main__':
    sys.exit(main())
