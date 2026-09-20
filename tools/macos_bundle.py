#!/usr/bin/env python3
"""The macOS application bundle, from a finished build directory.

    python3 tools/macos_bundle.py build-macos

leaves `build-macos/Snap64Recomp.app`, the zip
`Snap64Recomp-<version>-macos-<arch>.zip` beside it with a `.sha256`
sidecar, and inside the zip a START HERE text next to the bundle. In order:

1. `cmake --install <build> --prefix <build>/bundle-stage`: the flat,
   portable folder the install() rules lay out on every platform
   (BUILDING.md, step 13).
2. The bundle: the executable in Contents/MacOS, everything else in
   Contents/Resources with its folder names kept (menu_text/, cache/,
   gamecontrollerdb.txt, the icons, the licences, the texts), because a
   bundled program's SDL_GetBasePath is its Resources folder and that is
   where snap::exe_dir() reads the shipped files (src/paths.cpp). Nothing
   the port writes goes into the bundle: the data directory is
   ~/Library/Application Support/Snap64 Recomp/.
3. Contents/Info.plist, written here rather than kept as a file so the
   version in it is the build's (read from the generated version header).
4. An ad-hoc signature (`codesign --force --deep --sign -`), which is what
   the other N64 recompilations ship: no developer account and no
   notarisation, so the first launch is a right-click > Open, or on macOS 15
   System Settings > Privacy & Security > Open Anyway. `--no-sign` skips it.
5. The zip, made by ditto so the bundle survives the trip, and its SHA-256
   in the two-column form cpack writes.

Runs on macOS (codesign, ditto). `--dry-run <folder>` takes an already
installed folder in place of step 1 and stops before signing and zipping,
so the layout and the plist can be checked on any machine.
"""
import argparse
import hashlib
import os
import pathlib
import platform
import plistlib
import re
import shutil
import subprocess
import sys

APP_NAME = 'Snap64Recomp'
DISPLAY_NAME = 'Snap64 Recomp'
BUNDLE_ID = 'io.github.jackandbeans.snap64recomp'
MIN_SYSTEM = '14.0'          # CMAKE_OSX_DEPLOYMENT_TARGET in CMakeLists.txt
ICON_FILE = 'Snap64Recomp.icns'

START_HERE = """SNAP64 RECOMP FOR MACOS

This build has not been run on a Mac by me: read the last section.


1. THE APP
----------
Move Snap64Recomp.app to your Applications folder, or anywhere you like.


2. THE FIRST START
------------------
This build is signed by nobody, so macOS refuses a plain double-click.
Right-click (Control-click) the app, choose Open, then Open again in the
dialog. On macOS 15 (Sequoia) and later that no longer works: double-click
once, dismiss the message, open System Settings > Privacy & Security,
scroll to the note about Snap64Recomp and click Open Anyway. Once is enough.


3. THE ROM
----------
The port asks for your own Pokemon Snap (USA) cartridge dump on the first
start, checks it, and copies it into its data folder. .z64, .v64 and .n64
dumps are all accepted. No ROM is included and none ever will be.


4. WHERE THINGS ARE
-------------------
Everything the port writes lives in

    ~/Library/Application Support/Snap64 Recomp/

the ROM (pokemonsnap.z64), saves/, photos/, snapsettings.json, snap64.log
(and snap64.prev.log, the run before), mods/ and texture_packs/. In Finder,
Go > Go to Folder and paste the path. Nothing is written inside the app.


5. THE REST
-----------
The same as on Windows and Linux: the extras are in the game's own Options
screen, the keys and the pads are in the README inside the bundle
(Contents/Resources/README.md) and at github.com/JackandBeans/Snap64Recomp.


THIS BUILD IS UNVERIFIED
------------------------
I have no Mac. The macOS support was ported from a community build that ran
on an Apple M3, and this file's bundle was compiled by GitHub's own Mac;
whether it starts, draws and plays on your machine is the question nobody
has answered yet. Whichever way it goes, an issue at
github.com/JackandBeans/Snap64Recomp with snap64.log attached (the folder
above) is how it gets verified, or fixed.
"""


def die(msg):
    print('macos_bundle: ' + msg, file=sys.stderr)
    sys.exit(1)


def run(cmd, **kw):
    print('+ ' + ' '.join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True, **kw)


def read_version(build, override):
    """SNAP_PORT_VERSION from the header CMake writes (src/version.h.in), or
    the override; the prerelease suffix stays in the name and in
    CFBundleShortVersionString, and CFBundleVersion is the numbers alone."""
    if override:
        return override
    candidates = [build / 'generated' / 'version.h', build / 'version.h']
    candidates += sorted(build.glob('*/version.h'))
    for header in candidates:
        if header.is_file():
            m = re.search(r'#define\s+SNAP_PORT_VERSION\s+"([^"]+)"', header.read_text(encoding='utf-8'))
            if m:
                return m.group(1)
    cache = build / 'CMakeCache.txt'
    if cache.is_file():
        m = re.search(r'^CMAKE_PROJECT_VERSION:\w+=(\S+)$', cache.read_text(encoding='utf-8'), re.M)
        if m:
            return m.group(1)
    die('cannot find the version: no generated version.h under %s (pass --version)' % build)


def read_arch(build):
    cache = build / 'CMakeCache.txt'
    if cache.is_file():
        m = re.search(r'^CMAKE_OSX_ARCHITECTURES:\w+=(\S+)$', cache.read_text(encoding='utf-8'), re.M)
        if m and ';' not in m.group(1):
            return m.group(1)
    return platform.machine()


def lay_out(stage, app):
    """The install tree into the bundle. Returns the list of what went where."""
    exe = stage / APP_NAME
    if not exe.is_file():
        die('no %s in %s: run the install first (or point --dry-run at an installed folder)' % (APP_NAME, stage))
    if app.exists():
        shutil.rmtree(app)
    macos = app / 'Contents' / 'MacOS'
    resources = app / 'Contents' / 'Resources'
    macos.mkdir(parents=True)
    resources.mkdir(parents=True)
    shutil.copy2(exe, macos / APP_NAME)
    os.chmod(macos / APP_NAME, 0o755)
    placed = [('Contents/MacOS/' + APP_NAME)]
    for entry in sorted(stage.iterdir()):
        if entry.name == APP_NAME:
            continue
        if entry.name == 'START HERE.txt':
            # The Windows and Linux text: it says the ROM goes beside the
            # executable, which on a Mac it does not. The zip carries its own.
            continue
        target = resources / entry.name
        if entry.is_dir():
            shutil.copytree(entry, target)
        else:
            shutil.copy2(entry, target)
        placed.append('Contents/Resources/' + entry.name + ('/' if entry.is_dir() else ''))
    return placed


def write_plist(app, version):
    numbers = re.match(r'\d+(\.\d+)*', version)
    info = {
        'CFBundleDevelopmentRegion': 'en',
        'CFBundleDisplayName': DISPLAY_NAME,
        'CFBundleExecutable': APP_NAME,
        'CFBundleIconFile': ICON_FILE,
        'CFBundleIdentifier': BUNDLE_ID,
        'CFBundleInfoDictionaryVersion': '6.0',
        'CFBundleName': DISPLAY_NAME,
        'CFBundlePackageType': 'APPL',
        'CFBundleShortVersionString': version,
        'CFBundleVersion': numbers.group(0) if numbers else version,
        'CFBundleSupportedPlatforms': ['MacOSX'],
        'LSApplicationCategoryType': 'public.app-category.games',
        'LSMinimumSystemVersion': MIN_SYSTEM,
        'NSHighResolutionCapable': True,
        'NSHumanReadableCopyright': 'Snap64 Recomp is free software under the GNU GPL v3.',
        'NSPrincipalClass': 'NSApplication',
        'NSSupportsAutomaticGraphicsSwitching': True,
    }
    with open(app / 'Contents' / 'Info.plist', 'wb') as f:
        plistlib.dump(info, f, sort_keys=True)
    (app / 'Contents' / 'PkgInfo').write_bytes(b'APPL????')
    return info


def sign(app):
    # A cloud-synced or downloaded tree can carry Finder attributes that make
    # codesign refuse the bundle; strip them first, then seal everything.
    run(['xattr', '-cr', app])
    run(['codesign', '--force', '--deep', '--sign', '-', '--timestamp=none', app])
    run(['codesign', '--verify', '--deep', '--strict', '--verbose=2', app])


def zip_bundle(build, app, name):
    folder = build / name
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir()
    shutil.copytree(app, folder / app.name, symlinks=True)
    (folder / 'START HERE.txt').write_text(START_HERE, encoding='utf-8', newline='\n')
    zip_path = build / (name + '.zip')
    if zip_path.exists():
        zip_path.unlink()
    # ditto keeps the bundle's structure, permissions and resource forks in a
    # zip Archive Utility unpacks back into a working app.
    run(['ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', folder, zip_path])
    digest = hashlib.sha256(zip_path.read_bytes()).hexdigest()
    (build / (name + '.zip.sha256')).write_text('%s  %s\n' % (digest, zip_path.name), encoding='ascii', newline='\n')
    shutil.rmtree(folder)
    return zip_path, digest


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('build', help='the CMake build directory (build-macos)')
    ap.add_argument('--version', help='the version for the name and the plist (default: the build\'s version.h)')
    ap.add_argument('--arch', help='the machine in the zip name (default: CMAKE_OSX_ARCHITECTURES, else this machine)')
    ap.add_argument('--no-sign', action='store_true', help='leave the bundle unsigned')
    ap.add_argument('--dry-run', metavar='FOLDER',
                    help='an installed folder to lay out in place of cmake --install; stops before signing and zipping')
    args = ap.parse_args()

    build = pathlib.Path(args.build).resolve()
    if args.dry_run:
        stage = pathlib.Path(args.dry_run).resolve()
        build.mkdir(parents=True, exist_ok=True)
    else:
        if sys.platform != 'darwin':
            die('the bundle is assembled on macOS (codesign, ditto); on another machine use --dry-run <installed folder>')
        stage = build / 'bundle-stage'
        if stage.exists():
            shutil.rmtree(stage)
        run(['cmake', '--install', build, '--prefix', stage])

    version = read_version(build, args.version)
    arch = args.arch or read_arch(build)
    app = build / (APP_NAME + '.app')
    placed = lay_out(stage, app)
    info = write_plist(app, version)
    print('%s: %s %s, bundle %s' % (app, info['CFBundleName'], info['CFBundleShortVersionString'], info['CFBundleIdentifier']))
    for p in placed:
        print('  ' + p)
    if args.dry_run:
        print('dry run: not signed, not zipped')
        return
    if args.no_sign:
        print('not signed (--no-sign)')
    else:
        sign(app)
    name = '%s-%s-macos-%s' % (APP_NAME, version, arch)
    zip_path, digest = zip_bundle(build, app, name)
    print('%s\n  sha256 %s' % (zip_path, digest))


if __name__ == '__main__':
    main()
