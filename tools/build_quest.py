"""Build, sign, and optionally install the standalone Quest Release APK (Windows).

Requires Python 3, CMake/Ninja, a JDK 17+, Android SDK 34/build-tools 35,
NDK r27+, and the generated game/patch sources documented in BUILDING.md.
The ROM is used for compilation and optionally pushed separately; never packaged.
"""
from pathlib import Path
import argparse
import hashlib
import os
import secrets
import shutil
import subprocess
import sys
import zipfile
import xml.etree.ElementTree as ET
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = 'org.snap64.quest'


def run(*args, env=None, capture=False):
    args = [str(a) for a in args]
    print('>', subprocess.list2cmdline(args), flush=True)
    return subprocess.run(args, check=True, cwd=ROOT, env=env,
                          stdout=subprocess.PIPE if capture else None,
                          text=True).stdout


def require(path):
    path = Path(path).resolve()
    if not path.exists():
        raise RuntimeError(f'Missing prerequisite: {path}')
    return path


def newest(paths):
    paths = list(paths)
    if not paths:
        raise RuntimeError('Required tool not found; pass its path explicitly (see --help).')
    return sorted(paths, key=lambda p: p.stat().st_mtime)[-1]


def copy(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def quest_shader_compiler(build):
    # Pinned Microsoft release: unlike the old desktop toolchain it supports
    # runtime-format SPIR-V texel buffers required by the Quest renderer.
    directory=build/'toolchains/dxc-1.9.2607'
    executable=directory/'bin/x64/dxc.exe'
    if not executable.exists():
        directory.mkdir(parents=True,exist_ok=True)
        archive=directory/'dxc.zip'
        urllib.request.urlretrieve('https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2607/dxc_2026_07_29.zip',archive)
        if hashlib.sha256(archive.read_bytes()).hexdigest()!='a1dfb116ba3eeae6a1582291b53a8e7bf65ad760676bd3194685c8f7367cd241':
            raise RuntimeError('Quest shader compiler download checksum mismatch')
        with zipfile.ZipFile(archive) as package:package.extractall(directory)
    return require(executable)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', action='store_true')
    parser.add_argument('--launch', action='store_true', help='Install and launch the APK')
    parser.add_argument('--serial', help='ADB device serial; required if multiple authorized devices exist')
    parser.add_argument('--rom', type=Path, default=ROOT / 'pokemonsnap.z64')
    parser.add_argument('--sdk', type=Path)
    parser.add_argument('--ndk', type=Path)
    parser.add_argument('--java-home', type=Path)
    parser.add_argument('--host-build', type=Path, default=ROOT / 'build-win')
    parser.add_argument('--jobs', type=int, default=8)
    parser.add_argument('--benchmark', action='store_true', help='Isolated optimized controller-free benchmark APK')
    parser.add_argument('--validation-layer', type=Path, help='Package a Khronos Android arm64 validation layer in the benchmark only')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('This script currently uses the Windows host shader compiler.')
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    build = ROOT / 'build-quest'
    build.mkdir(exist_ok=True)
    sdk = require(args.sdk or os.environ.get('ANDROID_HOME') or os.environ.get('ANDROID_SDK_ROOT')
                  or Path(os.environ['LOCALAPPDATA']) / 'Android/Sdk')
    ndk = require(args.ndk or os.environ.get('ANDROID_NDK_HOME') or newest([
        *sdk.glob('ndk/27.*'), *sdk.glob('ndk/28.*'),
        *build.glob('toolchains/android-ndk-r27*')]))
    java = require(args.java_home or os.environ.get('JAVA_HOME') or newest(Path.home().glob('.jdks/*')))
    env = os.environ.copy()
    env['JAVA_HOME'] = str(java)
    env['PATH'] = str(java / 'bin') + os.pathsep + env['PATH']
    android_jar = require(sdk / 'platforms/android-34/android.jar')
    bt = require(sdk / 'build-tools/35.0.0')
    rom = require(args.rom)
    require(ROOT / 'RecompiledFuncs/funcs_0.c')
    require(ROOT / 'RecompiledPatches/patches.c')
    require(ROOT / 'patches/build/patches.bin')
    for name in ('cmake', 'ninja'):
        if not shutil.which(name):
            raise RuntimeError(f'{name} must be on PATH')

    adb_args = None
    if args.install or args.launch:
        adb = require(sdk / 'platform-tools/adb.exe')
        lines = run(adb, 'devices', capture=True).splitlines()[1:]
        devices = [line.split()[0] for line in lines if len(line.split()) >= 2 and line.split()[1] == 'device']
        serial = args.serial
        if serial and serial not in devices:
            raise RuntimeError(f'{serial} is not connected and authorized through ADB')
        if not serial:
            if len(devices) != 1:
                raise RuntimeError('Connect and authorize one Quest, or select it with --serial.')
            serial = devices[0]
        adb_args = [adb, '-s', serial]
        model = run(*adb_args, 'shell', 'getprop', 'ro.product.model', capture=True).strip()
        if 'Quest' not in model:
            raise RuntimeError(f'Selected device is {model}, not a Quest headset')

    host = args.host_build.resolve()
    rsp = list(host.rglob('RSPRecomp.exe')) if host.exists() else []
    file_to_c = list(host.rglob('file_to_c.exe')) if host.exists() else []
    if not rsp or not file_to_c:
        host = build / 'host'
        run('cmake', '-S', ROOT, '-B', host, '-DSNAP_ENABLE_VR=OFF', f'-DSNAP_ROM={rom}')
        run('cmake', '--build', host, '--config', 'Release', '--target', 'RSPRecomp', 'file_to_c', '-j', args.jobs)
        rsp, file_to_c = list(host.rglob('RSPRecomp.exe')), list(host.rglob('file_to_c.exe'))
    package = PACKAGE + ('.benchmark' if args.benchmark else '')
    native = build / ('arm64-benchmark' if args.benchmark else 'arm64')
    run('cmake', '-S', ROOT, '-B', native, '-G', 'Ninja',
        f'-DCMAKE_TOOLCHAIN_FILE={ndk / "build/cmake/android.toolchain.cmake"}',
        '-DANDROID_ABI=arm64-v8a', '-DANDROID_PLATFORM=android-29', '-DANDROID_STL=c++_shared',
        '-DCMAKE_BUILD_TYPE=Release', '-DSNAP_ENABLE_VR=ON', f'-DSNAP_ROM={rom}',
        f'-DSNAP_QUEST_BENCHMARK={"ON" if args.benchmark else "OFF"}',
        f'-DSNAP_HOST_DXC={quest_shader_compiler(build)}',
        f'-DSNAP_HOST_RSPRECOMP={require(rsp[0])}', f'-DSNAP_HOST_FILE_TO_C={require(file_to_c[0])}')
    run('cmake', '--build', native, '--target', 'Snap64Recomp', '-j', args.jobs)

    # A unique staging directory prevents deleted assets or Java classes from
    # leaking into later APKs. Build outputs and existing saves are never deleted.
    stage = build / ('package-' + secrets.token_hex(4))
    assets, classes, dex = stage / 'assets', stage / 'classes', stage / 'dex'
    for directory in (assets, classes, dex):
        directory.mkdir(parents=True)
    # Reuse the project's complete third-party attribution list.
    run('cmake', '--install', native, '--prefix', stage / 'runtime')
    for source in (stage / 'runtime/licenses').iterdir():
        if source.is_file():
            copy(source, assets / 'licenses' / source.name)
    for source in (ROOT / 'assets/vr').glob('*.json'):
        copy(source, assets / 'assets/vr' / source.name)
    for source in (native / 'quest-shaders').glob('*.spv'):
        copy(source, assets / 'shaders/quest' / source.name)
    for relative in ('menu_text/recomp_logo.png', 'assets/vr/NOTICE.md'):
        copy(ROOT / relative, assets / relative)
    copy(ROOT / 'shaders/rt64-seen-shaders.bin', assets / 'cache/rt64-seen-shaders.bin')
    copy(ROOT / 'assets/gamecontrollerdb.txt', assets / 'gamecontrollerdb.txt')
    if args.benchmark:
        copy(ROOT / 'tools/replays/beach.inputs', assets / 'benchmark/beach.inputs')
    for source in (ROOT / 'licenses').glob('*'):
        if source.is_file():
            copy(source, assets / 'licenses' / source.name)
    copy(ROOT / 'LICENSE', assets / 'licenses/Snap64.txt')
    copy(ROOT / 'NOTICE.md', assets / 'NOTICE.md')
    java_sources = sorted((ROOT / 'lib/SDL/android-project/app/src/main/java').rglob('*.java'))
    java_sources += sorted((ROOT / 'android/java').rglob('*.java'))
    source_list = stage / 'sources.txt'
    source_list.write_text('\n'.join('"' + p.as_posix() + '"' for p in java_sources), encoding='utf-8')
    run(java / 'bin/javac.exe', '-encoding', 'UTF-8', '--release', '8',
        '-classpath', android_jar, '-d', classes, '@' + str(source_list), env=env)
    class_jar = stage / 'classes.jar'
    run(java / 'bin/jar.exe', 'cf', class_jar, '-C', classes, '.', env=env)
    run(java / 'bin/java.exe', '-cp', bt / 'lib/d8.jar', 'com.android.tools.r8.D8',
        '--release', '--min-api', '29', '--lib', android_jar, '--output', dex, class_jar, env=env)
    unsigned, aligned = stage / 'unsigned.apk', stage / 'aligned.apk'
    manifest = ROOT / 'android/AndroidManifest.xml'
    if args.benchmark:
        ns = 'http://schemas.android.com/apk/res/android'
        ET.register_namespace('android', ns)
        tree = ET.parse(manifest)
        root = tree.getroot()
        root.set('package', package)
        app = root.find('application')
        app.set(f'{{{ns}}}label', 'Snap64 Quest Benchmark')
        ET.SubElement(app, 'meta-data', {f'{{{ns}}}name': 'com.android.graphics.injectLayers.enable', f'{{{ns}}}value': 'true'})
        app.find('activity').set(f'{{{ns}}}name', PACKAGE + '.QuestActivity')
        ET.SubElement(root, 'uses-feature', {f'{{{ns}}}name': 'oculus.software.handtracking', f'{{{ns}}}required': 'false'})
        ET.SubElement(root, 'uses-permission', {f'{{{ns}}}name': 'com.oculus.permission.HAND_TRACKING'})
        ET.SubElement(app, 'meta-data', {f'{{{ns}}}name': 'com.oculus.handtracking.version', f'{{{ns}}}value': 'V2.0'})
        manifest = stage / 'AndroidManifest.xml'
        tree.write(manifest, encoding='utf-8', xml_declaration=True)
    run(bt / 'aapt2.exe', 'link', '--manifest', manifest,
        '-I', android_jar, '-A', assets, '-o', unsigned)
    libraries = [require(native / 'libSnap64RecompVR.so'),
                 require(native / 'lib/SDL/libSDL2.so'),
                 require(newest(native.glob('_deps/openxr-build/**/libopenxr_loader.so'))),
                 require(ndk / 'toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so')]
    if args.validation_layer:
        if not args.benchmark:
            raise RuntimeError('Validation layers are restricted to the isolated benchmark APK')
        libraries.append(require(args.validation_layer))
    with zipfile.ZipFile(unsigned, 'a', compression=zipfile.ZIP_DEFLATED) as apk:
        apk.write(require(dex / 'classes.dex'), 'classes.dex')
        for library in libraries:
            stripped = stage / library.name
            copy(library, stripped)
            run(ndk / 'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe', '--strip-unneeded', stripped)
            apk.write(stripped, 'lib/arm64-v8a/' + library.name)
    run(bt / 'zipalign.exe', '-f', '-p', '4', unsigned, aligned)

    # Stable personal signing identity, outside the repository. Passwords are
    # passed via the child environment and never printed in the command log.
    signing = Path.home() / '.android/snap64-quest'
    signing.mkdir(parents=True, exist_ok=True)
    key, password_file = signing / 'release.keystore', signing / 'password.txt'
    if key.exists() and not password_file.exists():
        raise RuntimeError(f'Missing signing password: {password_file}')
    if not password_file.exists():
        password_file.write_text(secrets.token_urlsafe(32), encoding='utf-8')
    env['SNAP_QUEST_KEY_PASSWORD'] = password_file.read_text(encoding='utf-8').strip()
    if not key.exists():
        run(java / 'bin/keytool.exe', '-genkeypair', '-keystore', key, '-alias', 'snap64',
            '-storepass:env', 'SNAP_QUEST_KEY_PASSWORD', '-keypass:env', 'SNAP_QUEST_KEY_PASSWORD',
            '-keyalg', 'RSA', '-keysize', '2048', '-validity', '10000',
            '-dname', 'CN=Snap64 Quest Local Release', env=env)
    apk = build / ('Snap64RecompVR-quest-benchmark.apk' if args.benchmark else 'Snap64RecompVR-quest-release.apk')
    signer = [java / 'bin/java.exe', '-jar', bt / 'lib/apksigner.jar']
    run(*signer, 'sign', '--v2-signing-enabled', 'true', '--ks', key, '--ks-key-alias', 'snap64',
        '--ks-pass', 'env:SNAP_QUEST_KEY_PASSWORD', '--out', apk, aligned, env=env)
    run(*signer, 'verify', '--verbose', apk, env=env)
    digest = hashlib.sha256(apk.read_bytes()).hexdigest()
    apk.with_suffix('.apk.sha256').write_text(f'{digest}  {apk.name}\n')
    print(f'Release APK: {apk}', flush=True)
    if adb_args:
        run(*adb_args, 'install', '--no-incremental', '-r', apk)
        destination = f'/sdcard/Android/data/{package}/files'
        run(*adb_args, 'shell', 'mkdir', '-p', destination)
        run(*adb_args, 'push', rom, destination + '/pokemonsnap.z64')
        if args.launch:
            run(*adb_args, 'shell', 'am', 'start', '-n', package + '/' + PACKAGE + '.QuestActivity')
        print('Installed on Quest; ROM copied separately. Existing saves retained.')


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print(f'Quest build failed: {error}', file=sys.stderr)
        sys.exit(1)
