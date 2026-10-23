# Building Snap64 Recomp

This is the build as it exists today, on the one machine it has ever been built
on. It is not a one-command build, and a fresh `git clone` does not contain
everything the build needs. Read [What a clean checkout is missing](#what-a-clean-checkout-is-missing)
first; the rest of this file is the full pipeline, in order, with the commands
that were actually used or, where a step was done by hand and not recorded, the
command derived from the tool's own rules (each such case is marked).

Two machines are involved:

* **WSL (Ubuntu 24.04)** runs the decompilation, the IDO compiler, the
  cross-linker and N64Recomp. Everything that reads the ROM or the
  decompilation's ELF happens here.
* **Windows** runs CMake and MSVC and produces the executable. The port root is
  a Windows directory (`C:\Users\<you>\PokemonSnapRecomp`), reached from WSL as
  `/mnt/c/Users/<you>/PokemonSnapRecomp`.

Steps 1 to 9 are the same on every platform. Steps 10 to 13 are the Windows
build; [step 14](#14-linux-build-experimental) is the Linux one, which has
been built and started under WSL and not yet run on real Linux hardware;
[step 15](#15-macos-build-githubs-mac) is the macOS one, which carries the
glue a community Apple Silicon build proved and is built by a workflow on
GitHub's Mac, because I have no Mac: no build of this tree has run on one
yet; [step 16](#16-android-cross-build-experimental) is the Android one,
an app built around the port as an Android library, which has run on
Google's emulator and on one phone, a Galaxy S25 Ultra in Samsung's Remote
Test Lab (2026-10-21), and has met no real pad yet (only one made in
software on the emulator).

## Prerequisites

### Windows

* Visual Studio 2019 with the C++ workload (the recorded build used MSVC
  19.29.30159 through the `Visual Studio 16 2019` generator, platform `x64`).
  The suite's speed check, the scoring tape at 3x against a run at 1x, is
  not always repeatable on the release machine (2026-10-22): the 3x run
  matched a 1x reference 3 times in 11 with an SDL2.dll built by VS 2019,
  and 14 times in 14 with GitHub's or one built there by VS 2022 (MSVC
  14.44); a 1x run with the photos exported drifted from that reference in
  2 of 4 runs, whichever SDL2.dll, and a VS 2022 build failed the check in
  the suite too. Why the tape drifts is not known; a failed speed check is
  rerun alone.
* CMake 3.20 or newer (the recorded build used 4.4.2).
* Python 3 (any recent version). The build-pipeline scripts (`fetch_deps`,
  `gen_reference_syms`, `gen_overlays`, `hook_funcs`) use only the standard
  library; the asset and checking scripts need Pillow, NumPy and SciPy
  (`tools/README.md`).
* Git for Windows on `PATH`, and access to github.com for
  `tools/fetch_deps.py` (step 10).
* A short path for the checkout, such as `C:\src\Snap64Recomp`. The longest
  path in a checkout is 184 characters (a SPIR-V Cross test file inside
  RT64's trees), the build directory adds more, and Windows refuses paths
  past 260 unless long paths are enabled. `tools/fetch_deps.py` turns git's
  `core.longpaths` on in the checkout and in every submodule, so git copes
  with a deeper folder; the compiler and the build do not, so a short path
  is still the way. `git config --global core.longpaths true` together with
  Windows' long-path setting is the other way around it.

### WSL

* Ubuntu 24.04 with `binutils-mips-linux-gnu` (`mips-linux-gnu-ld` 2.42 and
  `mips-linux-gnu-as` are what the decomp and the patch build call),
  `binutils` (`readelf`), `python3`, `ninja`, and `uv` for the decomp's Python
  environment.
* **The decompilation**: <https://github.com/ethteck/pokemonsnap>, cloned to
  `~/pokemonsnap` (the tools default to that path; set `SNAP_DECOMP` to point
  the harvest tools elsewhere, and pass `DECOMP=` to the patch Makefile).
  The port was last generated against its commit `3a236dc` ("Matched
  drawbitmap"). Its setup (`uv run configure.py --setup`) downloads the
  IDO 7.1 and IDO 5.3 compilers as static recompilations from
  <https://github.com/decompals/ido-static-recomp/releases> (release v1.1) into
  `tools/ido7.1` and `tools/ido5.3`; the patch build uses that `tools/ido7.1/cc`
  directly. IDO is SGI's proprietary compiler and is not part of this
  repository.
* **N64Recomp**: <https://github.com/N64Recomp/N64Recomp>, cloned to
  `~/N64Recomp` and built in Release (`cmake -S . -B build
  -DCMAKE_BUILD_TYPE=Release && cmake --build build`), which produces
  `build/N64Recomp` (and a `build/RSPRecomp` this port no longer uses: the
  Windows build compiles its own RSPRecomp from the vendored copy, step 9).
  The recorded build used upstream commit `ffb39cd` with no local changes.
* **Your ROM**: a dump of the US cartridge, SHA-1
  `edc7c49cc568c045fe48be0d18011c30f393cbaf` (the checksum the decomp publishes
  and checks against its rebuilt ROM). It is never distributed with this
  project.

## The pipeline

### 1. Build the decompilation

Follow the decomp's README: place the ROM at `~/pokemonsnap/pokemonsnap.z64`,
then `uv sync`, `uv run configure.py --setup`, `uv run configure.py`, `ninja`.
That leaves `build/pokemonsnap.elf` and `build/pokemonsnap.z64`; ninja's
`pokemonsnap.ok` rule checks the rebuilt ROM's SHA-1 against the original, so a
finished build is a matching one.

The tree must be **unmodified** (`git status` clean apart from the
`include/rt64_extended_gbi.h` header that lives there untracked). Every address
in this port -- the `manual_funcs` in `pokemonsnap.us.toml`, the overlay table,
the two symbol files, the patches -- assumes the ROM's own layout. Growing
`.main` by even 0x50 bytes shifts every section after it; see
`patches/README.md`, "The earlier route, and why it was abandoned", for the record of exactly that mistake.

### 2. Relink with relocations

N64Recomp's ELF mode needs the relocations kept in the file. The decomp's link
rule (`build.ninja`, `rule ld`) is

    mips-linux-gnu-ld -T undefined_syms.txt -T undefined_syms_auto.txt -Map build/pokemonsnap.map -T pokemonsnap.ld -o build/pokemonsnap.elf

and the relocatable ELF is the same link with `--emit-relocs`:

    cd ~/pokemonsnap
    mips-linux-gnu-ld -T undefined_syms.txt -T undefined_syms_auto.txt \
        -Map build/pokemonsnap.relocs.map -T pokemonsnap.ld --emit-relocs \
        -o build/pokemonsnap.relocs.elf

**This command is derived, not recorded.** Neither repository contains a script
for the relink; it was done by hand. The one thing that is certain is the
input the port was generated from: an ELF whose `.main` is 0x45270 bytes
starting at ROM 0x1000, with the `.rel.*` sections interleaved so that `.main`
is section header 3, `.main_bss` is 5 and `.app_render` is 6 -- the section
indices baked into `src/recomp_overlays.inl`.

**Check the result before using it.** On the machine this was written on,
`~/pokemonsnap/build/pokemonsnap.relocs.elf` is a stale link from a *patched*
tree: its `.main` is 0x452C0 bytes and its first 0x45270 bytes differ from the
ROM's at byte 244, while `build/pokemonsnap.elf` (relinked later from the
clean tree) matches the ROM byte for byte. Regenerating anything from that
stale file would shift 122 of the overlay table's 156 rows. The check:

    cmp <(dd if=build/pokemonsnap.relocs.elf bs=1 skip=$((0x20400)) count=$((0x45270)) 2>/dev/null) \
        <(dd if=pokemonsnap.z64 bs=1 skip=$((0x1000)) count=$((0x45270)) 2>/dev/null) && echo ".main matches the ROM"

`tools/gen_overlays.py` also warns when the ELF's code sections disagree with
the tracked `patches/pokemonsnap.syms.toml`.

### 3. Put the inputs in the port root

N64Recomp's config uses paths relative to its own directory (`src/config.cpp`
joins every entry with the config file's parent directory), so its two inputs
go in the port root under fixed names. Both are ignored by git (`*.elf`,
`*.z64`); the ROM placed here is also the first place CMake looks for
`SNAP_ROM` (step 9):

    cd /mnt/c/Users/<you>/PokemonSnapRecomp
    cp ~/pokemonsnap/build/pokemonsnap.relocs.elf .
    cp ~/pokemonsnap/pokemonsnap.z64 .

Keep the ELF's name: N64Recomp writes its stem into
`RecompiledFuncs/lookup.cpp` (`get_rom_name` returns `pokemonsnap.relocs.z64`;
nothing reads it, but renaming the file changes generated output).

No hand edits in the generated code: what the port changes in it,
`tools/hook_funcs.py` changes after every generation (step 5), and it stops
the build when a change no longer finds its place. That includes
`auThreadMain`'s two reads of the audio interface's length register, which
it turns into calls that ask the audio queue.

### 4. Recompile the game

    ~/N64Recomp/build/N64Recomp pokemonsnap.us.toml

Writes `RecompiledFuncs/` (86 files: `funcs_*.c`, `funcs.h`, `lookup.cpp`,
`recomp_overlays.inl`). All 32 code sections are recompiled as relocatable
(`overlays.us.txt`); the file's header comment says why. N64Recomp only rewrites
a `funcs_*.c` whose content changed, so file dates in that directory are not a
record of the last run.

### 5. Rename the hooked functions

    python3 tools/hook_funcs.py

MSVC has no `--wrap`, so the port intercepts game functions by renaming the
generated definition to `__game_<name>` and defining `<name>` itself
(`src/overlay_hook.cpp`, `src/matrix_tags.cpp`, ...). Between the two the
script generates `__real_<name>` (`RecompiledFuncs/funcs_snap_hooks.c`, with
`snap_host_hooks.h`), which the port's `<name>` calls: it runs the hooks mods
put on the function around the game's code, so a mod's hook keeps the port's
work in it (`src/host_hooks.cpp`). The script edits
`RecompiledFuncs/funcs_*.c` and `funcs.h` in place, is idempotent, and also
inserts the inner hooks listed in its `INNER_HOOKS` table. It also finishes
`RecompiledPatches/recomp_overlays.inl` for mods' hooks (step 8). Run it after
every step 4 and every step 8.

### 6. Regenerate the overlay table (only when step 4 changed anything)

    python3 tools/gen_overlays.py pokemonsnap.relocs.elf

Writes `src/recomp_overlays.inl`, which is tracked. The table is wider than
N64Recomp's own (every ALLOC section of the ELF, data and bss included, with
`.index` equal to the ELF section header index) and keeps every function of
the recompiler's arrays but the renamed entrypoint; the tool's docstring
lists every rule. When only the functions changed, `python3
tools/gen_overlays.py --arrays-only` rewrites the function arrays from
`RecompiledFuncs` and leaves the section rows alone, with no ELF needed. Against the recompiler output in this tree the tool
reproduces the tracked file exactly, apart from the address shifts caused by
the stale ELF described in step 2.

### 7. Symbol files for the patch build (only when the decomp's symbols change)

    python3 tools/gen_reference_syms.py ~/pokemonsnap/build/pokemonsnap.elf \
        patches/pokemonsnap.syms.toml patches/game_syms.ld

Both outputs are tracked. They contain only names, addresses and sizes: 4,268
functions across 32 code sections, and 14,100 `PROVIDE(name = 0x...)` lines for
data. Use `build/pokemonsnap.elf` (the plain link that matches the ROM); the
relocatable ELF carries the same symbols only if it was linked from the same
tree. A fourth argument writes the data symbols a mod build needs, in the
mod tool's form (`docs/MODS.md`); the Snap64RecompSyms repository is the
functions file and that data file, regenerated together whenever these are.
Each section's `rom` is its ROM address, the LOAD segment's physical address
plus the offset inside it, which is what the runtime's section table and the
mod tool agree on.

### 8. Build and recompile the game-side patches

    make -C patches DECOMP=$HOME/pokemonsnap
    ~/N64Recomp/build/N64Recomp patches.toml
    python3 tools/hook_funcs.py

The first compiles `patches/src/*.c` with the decomp's IDO 7.1 against the
decomp's headers and links `patches/build/patches.elf` with
`mips-linux-gnu-ld` (`patches/Makefile`, `patches/patch.ld`, `patches/game_syms.ld`).
The first also writes `patches/build/patches.bin`, the ELF's loadable bytes;
CMake embeds them in the executable and librecomp copies them into memory at
start-up, which is how the patches' `.data` section -- every float literal
and table IDO puts there -- becomes readable (`patches/patch.ld`,
`src/main.cpp`). The bytes come from a second link of the same objects with
every game function defined at its address (`tools/gen_patch_funcs_ld.py`):
in `patches.elf` a call into the game is left as zero for the recompiler to
match, but the runtime recompiles a patched function live from these bytes
when a mod hooks it, so there the calls must be real. The Makefile checks
that the two links lay out the same.

The second writes `RecompiledPatches/patches.c`, which CMake compiles straight
into the executable so that each patched function is resolved before the
linker reaches the recompiled game. `patches/README.md` explains the mechanism.

The third finishes `RecompiledPatches/recomp_overlays.inl`, the table the
runtime reads when a mod hooks a function the patches replace: it renumbers
the relocations' sections from the symbol file's order to the port's own
table's, and lists the patches' unnamed static functions (`finish_patch_table`
in `tools/hook_funcs.py`). `src/main.cpp` will not build without it.

### 9. The audio microcode (generated by the build, from your ROM)

Nothing to run by hand. `aspMain`, the game's RSP audio microcode (ROM
0x3E580, 0xE20 bytes, loaded at IMEM 0x1080), is recompiled to C++ during the
Windows build: CMake builds RSPRecomp from the vendored
`lib/N64ModernRuntime/N64Recomp`, configures `rsp/aspMain.us.toml.in` into
`build-win/rsp/aspMain.us.toml` with absolute paths, and runs it to write
`build-win/rsp/aspMain.cpp`, which is compiled into the executable. The
translation is derived from the ROM and is never committed (`.gitignore`
refuses `rsp/aspMain.cpp`); `NOTICE.md` says so under "Material derived from the game, and where it comes from".

The ROM is named by the `SNAP_ROM` cache variable. Left empty, configure
fills it from the first of `pokemonsnap.z64` in the port root (where step 3
put it), `build-win/Release/pokemonsnap.z64` (where the executable runs from)
and `build-win/pokemonsnap.z64` that exists, and stops with an error naming
all three places when none does. It must be a big-endian `.z64` whose header
name is `POKEMON SNAP`; a byte-swapped or little-endian dump is refused at
configure time. Pass `-DSNAP_ROM=<path>` to name another file. The ROM is a
build dependency: changing it, the template or RSPRecomp regenerates the
microcode on the next build.

### 10. Vendored trees

`lib/rt64`, `lib/N64ModernRuntime`, `lib/SDL` and `lib/DirectX-Headers` are
git submodules, and more are nested inside the first two (RT64's
`src/contrib`, the runtime's N64Recomp and third-party trees): 33 in all,
each at an exact commit (`VENDORING.md`). They must be checked out before
CMake runs. One command does it:

    python tools/fetch_deps.py

It runs `git submodule update --init --recursive`, one commit deep, and
checks that every checkout sits at its recorded commit; the first run took
172 seconds on the machine this was written on, and a second run is a no-op
(`--list` prints every commit, `--full` fetches whole histories). It needs
git on `PATH` and access to github.com. A `git clone --recursive` fetches
the same trees, but it does not do the two things the script adds:

* every submodule is checked out as its repository stores it, with
  `core.autocrlf=false` set in each, so the build reads the same bytes on
  every machine (Git for Windows otherwise converts text to CRLF);
* `dxc/bin/x64/dxil.dll` is replaced with the file of Microsoft's release
  v1.7.2308, downloaded from the release archive (25 MB) and checked by
  SHA-256, because the copy in rt64's `dxc-bin` is under terms that do not
  allow distributing it (`VENDORING.md`, "dxc"); the dxc binaries are then
  checked file by file against SHA-256 values in the script.

RT64, the runtime, plume and N64Recomp are Snap64 Recomp's forks of them,
with the port's changes on each fork's `snap64` branch; `VENDORING.md`,
"Changing RT64 or the runtime", says how to change one.

### 11. CMake

    cmake -S . -B build-win -G "Visual Studio 16 2019" -A x64
    cmake --build build-win --config Release --target Snap64Recomp --parallel

Configure prints `RSP audio microcode from ROM: <path>` once it has found the
ROM (step 9), or stops with a message naming `SNAP_ROM` and the places it
looked. `CMakeLists.txt` globs `RecompiledFuncs/funcs_*.c` into the
`recomp_funcs` static library, builds RSPRecomp and runs it to write
`build-win/rsp/aspMain.cpp`, compiles that and `RecompiledPatches/patches.c`
(if present) into the executable, links RT64 statically and links against the
SDL2 built from `lib/SDL`. The result is `build-win/Release/Snap64Recomp.exe`,
with `Snap64Recomp.map` (the linker map, `/MAP`) beside it.

Configure also writes `build-win/generated/version.h` and
`build-win/generated/snap64.rc` from `src/version.h.in` and
`src/snap64.rc.in`. The version is typed once, in `CMakeLists.txt`
(`project(Snap64Recomp VERSION 1.0.0)` plus `SNAP_VERSION_PRERELEASE`: `""`
for a release, `rc1` and so on for the candidates before it), and reaches
the title bar, the log banner, the title screen's credits line, the
executable's version resource (Properties > Details) and the package name
from there. The same resource file compiles in the executable's icon,
`src/snap64.ico`, composed from the logo (`docs/logo.png`) by
`tools/icon_gen.py` (Pillow); the `.ico` is tracked, so the generator runs
only when the logo changes.

### 12. Runtime files next to the executable

The build stages these itself (a `POST_BUILD` step of the `Snap64Recomp`
target in `CMakeLists.txt`); the ROM is the one file to copy by hand:

| File | Comes from |
| --- | --- |
| `SDL2.dll` | the `SDL2` target built from `lib/SDL` (step 11) |
| `dxcompiler.dll`, `dxil.dll` | `lib/rt64/src/contrib/dxc/bin/x64/` (the same files RT64's CMake copies into `build-win/lib/rt64/`) |
| `menu_text/recomp_logo.png` | the repository's `menu_text/` (the title-screen badge; absent file, absent badge) |
| `menu_text/pointer.png`, `menu_text/pointer_flash.png` | the repository's `menu_text/` (the mouse pointer's four frames and its flash's four; absent pointer file, the system's pointer; absent flash file, no flash) |
| `pokemonsnap.z64` | your ROM: copy it next to the executable yourself |

The executable also loads `d3d12.dll`, `dxgi.dll` and `vulkan-1.dll` from the
system. Saves go to `saves/`, settings to `snapsettings.json` and RT64's
shader and pipeline caches to `cache/`, all next to the executable whatever
the working directory: `src/paths.cpp` resolves the executable's directory
(`SDL_GetBasePath`), `src/main.cpp` registers it as librecomp's config path
(ROM, saves, mods), and `src/rt64_render_context.cpp` hands RT64 `cache/`
under it as its data path. Diagnostics driven from a shell (`SNAP_REPLAY`,
`SNAP_RECORD`, `snap_frame_dumps/`, `ramdump*.bin`, `menu_font_runtime.json`)
still resolve against the working directory.

### 13. Package

    cd build-win
    cpack -C Release

writes `Snap64Recomp-1.0.0-win64.zip` and a `.sha256` beside it in
`build-win`. The ZIP holds one folder of the same name: `Snap64Recomp.exe`,
`Snap64Recomp.map`, the three DLLs, `menu_text/recomp_logo.png`,
`menu_text/pointer.png`, `LICENSE`,
`NOTICE.md`, `README.md` and `licenses/` -- one `.txt` per component in
`NOTICE.md`, copied from the vendored trees at packaging time so they cannot
drift from what was built (`concurrentqueue.txt` is cut from that header's
leading comment at configure time). No ROM, no saves, no settings, no cache.

Four license texts come from the repository's `licenses/` instead, because the
vendored copies carry none (`DirectXShaderCompiler-dxil.txt`, Microsoft's terms
for `dxil.dll`, and `roboto.txt`, the Apache 2.0 text for the printer's lettering,
are the other two; `NOTICE.md` describes both):

* `licenses/nlohmann-json.txt` -- tracked: the MIT text with the copyright
  line from `json.hpp`'s SPDX header.
* `licenses/DirectXShaderCompiler.txt` -- tracked: the `dxc` binaries under
  `lib/rt64/src/contrib/dxc/` ship without their `LICENSE.TXT`, so the text
  was taken from upstream's
  <https://raw.githubusercontent.com/microsoft/DirectXShaderCompiler/main/LICENSE.TXT>
  on 2026-09-02 (the LLVM Release License, University of Illinois/NCSA). If
  the file is ever removed, configure warns and `cpack` stops on it: a package
  without the DXC license is not meant to be produced.

To cut a release (from the release after 1.1.0 on, the archives are
GitHub's, not this machine's):

1. Change `project(Snap64Recomp VERSION ...)` in `CMakeLists.txt`, grep
   `README.md` and `BUILDING.md` for the old version string, and commit.
2. Rebuild the patches on the release tree (step 8), lay the private
   inputs out with `python tools/ci_inputs.py <the inputs repository's
   working copy>`, and push that repository (step 15).
3. Put the release tree on `macos-build` as one commit on top of `main`
   (`git commit-tree <tree> -p origin/main`, then push it there), and run
   `gh workflow run release.yml --ref macos-build`. GitHub builds the
   archives, signs each with its build provenance, runs the Mac's checks
   and the package checks over all of them, verifies each signature, and
   keeps them as the run's artifact `release-archives`, their checksums in
   the run's summary. The Android APK among them is signed with the release
   key (step 16) when the repository's three secrets `SNAP64_ANDROID_KEYSTORE`
   (the keystore in base64), `SNAP64_ANDROID_KEYSTORE_PASSWORD` and
   `SNAP64_ANDROID_KEY_ALIAS` exist; without them it comes unsigned, as
   `Snap64Recomp-<version>-android-unsigned.apk`, and step 4 signs it.
4. `gh run download <run> -n release-archives`, unpack the Windows zip, put
   the ROM and `build-win/Release/saves` beside the executable, and run
   `python tools/release_check.py <that folder>` and `--only station` on it.
   An unsigned APK is signed here first, under WSL or on Linux:
   `tools/release/sign_apk.sh <it> <keystore>` asks for the key's password,
   signs with the v2 and v3 schemes, verifies, writes
   `Snap64Recomp-<version>-android.apk` with its sidecar and runs the
   package check. The signature comes after GitHub's build provenance, so
   `gh attestation verify` holds for the unsigned file, not for the signed
   one. When no one is at the release machine to type the password, the
   release's APK can instead be the release machine's own build of the same
   commit, signed beforehand with the same key and checked the same way;
   the page says the same of either: signed on the release machine, no
   GitHub build record. The APK from the same files goes through `tools/android_check.py` on the
   emulator (every stage, the pad stage with `--uipad`; step 16) and
   `tools/android/paths_check.py` (the Android-only paths, by touch), and,
   when a test lab's phone can be had, through every stage the phone allows
   there; the release page says which ran.
5. Fast-forward `main` to the candidate commit, `git tag -a v<version>` on
   it, push both, and publish those same files with their `.sha256`
   sidecars.

Anyone can check a published file with `gh attestation verify <file> --repo
JackandBeans/Snap64Recomp`: it names the commit and the workflow that made
it. An APK signed on the release machine (step 4) is the exception: its
signature changed the file after GitHub built it. A build that must not pass for the release takes a prerelease label
(`-f prerelease=rc1`), and every surface then says `<version>-rc1`. The
label is drawn on the title screen in the credits face, so it uses only the
characters that face has: every digit, dots, and of the lower-case letters
`a c d e i k m n o p r s t u v` (`rc1`, `pre2`, `test3`; not `beta1` or
`dryrun1`); CMake refuses any other, and a label that slipped through would
make the port withhold its whole title dress, the "Recomp" badge and the
credits line together, as `dryrun1` did on the workflow's first run. The
Windows executable is built with `/d1trimfile`, the Linux and Mac ones with
`-ffile-prefix-map` and the Mac's link with `-oso_prefix` (until 1.1.0 they
were not, and carried the build machine's folders), and the shipped linker
map is filtered, so no build path reaches an archive; the package check
looks for one in all three.
The credits face on the title screen is harvested from the copyright block
and the port draws the digits it lacks, so any version number renders; a
hyphen or a letter outside the set above is reported at the first main-menu
load (`[SNAP-MENU] no glyph ...`) and the port's menu strings are withheld
until the string is changed (`src/version.h.in`).

### 14. Linux build (experimental)

The same tree builds on Linux with GCC, from the same generated inputs
(steps 1 to 9: `RecompiledFuncs/`, `RecompiledPatches/`,
`patches/build/patches.bin`, the vendored trees from `tools/fetch_deps.py`).
Recorded on 2026-09-06 under WSL, Ubuntu 24.04, Clang 18.1 (GCC 13.3
builds it too; the other N64 recompilations all build with Clang, and the
one that drew frames here is the Clang build), CMake 3.28, in a copy of the
port root on the Linux filesystem (`rsync` it there; compiling 32 MB of
generated C through `/mnt/c` is slow):

    sudo apt install build-essential clang lld cmake ninja-build libsdl2-dev libgtk-3-dev libvulkan-dev
    cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DSNAP_ROM=/path/to/pokemonsnap.z64
    cmake --build build-linux --target Snap64Recomp -j"$(nproc)"
    cd build-linux && cpack -G TGZ

`libgtk-3-dev` is for RT64's file-dialog library, which the port never calls
but RT64 links; `libvulkan-dev` for plume's Vulkan backend. The package is
`Snap64Recomp-<version>-linux-x86_64.tar.gz` with a `.sha256` beside it: the
same flat folder as the Windows ZIP, without the DLLs and the linker map. At
run time the binary needs the system's `libSDL2-2.0.so.0` (2.26 or newer;
SteamOS 3.8 provides it through sdl2-compat), GTK 3 and a Vulkan driver
(SDL loads the loader); it ships no shader compiler, because off Windows
RT64 specializes its SPIR-V shaders with re-spirv instead of DXC. The C++
runtime is linked in (`-static-libstdc++ -static-libgcc`, as the other N64
recompilations do); glibc is not, so the build machine's glibc is the
floor (2.39 here; SteamOS 3.8 ships 2.41). The recompiled game, the
patches and the port are compiled with `-fno-strict-aliasing`: emulated
RAM is read through several widths at once, and a contributor's Clang
build crashed in the audio start-up without it.

What differs from the Windows build, all in `#if` branches of the same
files:

* Rendering is Vulkan only; `graphics_api` in the settings file is ignored
  (`src/rt64_render_context.cpp`).
* `RecompiledPatches/patches.c` is compiled from a copy in the build tree
  in which the two libultra names glibc's `math.h` reserves, `__sinf` and
  `__cosf`, are the game's own `__sinf_recomp` and `__cosf_recomp`, and
  every game function the patches call has a prototype (the recompiler
  leaves them implicit, which Clang 16 and later reject)
  (`tools/portable_patches.cmake`; the copy is made on Windows too, and the
  shim that used to bridge the names is gone).
* `lib/rt64/src/contrib/plume/plume_vulkan.cpp` carries one of the port's
  changes to plume (the plume fork, VENDORING.md): upstream's Vulkan backend has
  no texture-to-buffer copy, which the presented-frame capture and the
  Snap Station's sheet capture use, and dereferenced a null texture.
* `snap64.log` in the data directory is written on every launch: both
  streams go to the file, unbuffered. When stdout was a terminal, a pipe,
  a file or a socket, a thread follows the file and copies every line to a
  duplicate of that descriptor; `SNAP_LOG_ECHO_FD` carries the duplicate
  across the Snap Station's relaunch, which keeps the log open. One copy
  at a time is a `flock` on `snap64.lock` in the data directory. The Snap
  Station's relaunch `execv`s `/proc/self/exe` in place, keeping the pid
  so Steam keeps the game as running, and the sheet's folder opens with
  `xdg-open` (`src/main.cpp`, `src/snap_station.cpp`).
* The data directory is the executable's when it can be written, else
  `$XDG_CONFIG_HOME/Snap64Recomp` (`~/.config/Snap64Recomp`);
  `SNAP_DATA_DIR` names one outright on every platform (`src/paths.cpp`,
  the one part of this that Windows shares). The files the
  port ships and only reads (`gamecontrollerdb.txt`, `menu_text/`, the
  icons, the seed of the seen-shader list) are read from the executable's
  directory, `snap::exe_dir()`, whichever the data directory is.
* `src/steam_deck.cpp`: `SteamDeck=1` in the environment (Steam sets it
  for a game it launches on a Deck, through Proton too) or a board vendor
  of `Valve` under `/sys` means a Steam Deck, which boots fullscreen
  through the same live path the Snap Station's restore uses. SDL's screen
  keyboard is turned off before init and text input stopped after the
  window exists, so Steam's on-screen keyboard does not open over the game.
  Inside gamescope (`GAMESCOPE_WAYLAND_DISPLAY` set) the port asks for the
  display's own size at window creation: it reads its X server's id from
  the root window property `GAMESCOPE_XWAYLAND_SERVER_ID` and writes
  `GAMESCOPE_XWAYLAND_MODE_CONTROL` (id, an oversized width and height,
  and a zero that makes gamescope clamp both to the display), the request
  Steam's Game Resolution "Native" makes; gamescope resizes the nested X
  screen and the fullscreen that follows takes it (`snap::gamescope_request_output_size`,
  libX11 through the copy SDL loaded).
* On Windows under Wine (Proton), `SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD`
  is set before SDL initializes, so Steam's virtual controller is seen.
* `lib/rt64/src/hle/rt64_snap_diag.h` and `rt64_application.cpp` carry the
  two portability guards a contributor's macOS build found first: `mkdir`
  in place of `_mkdir`, and the pipeline counters defined where no D3D12
  backend defines them.

A start-up check without a window:

    cd build-linux && SDL_VIDEODRIVER=dummy ./Snap64Recomp

prints the ROM check, `[SNAP] graphics API: Vulkan` and the device RT64
found (`llvmpipe` where there is no GPU), then fails to create the window,
which is the dummy driver's doing. With a display (WSLg will do) the Beach
replay runs and the presented-frame capture writes real pictures:

    SNAP_REPLAY=beach.inputs SNAP_MUTE=1 SNAP_WINDOW=640x480 SNAP_PCAP_ATFRAME=600,1200,1800 SNAP_PCAP_AT=9999999 timeout 90 ./Snap64Recomp

(the replay beside the binary, from `tools/replays/`). On 2026-09-06 that
drew Oak's lab correctly on `llvmpipe`. WSL's audio sink accepts samples
and never plays them; the port drops the queue and says so
(`[SNAP-Audio] the device is not draining`), which is the guard that also
covers a Deck's sleep or a Bluetooth switch. Running the game on a Linux
desktop or a Steam Deck is [unverified](docs/STEAM-DECK.md).

GitHub's machines make the archives too. `.github/workflows/build.yml`
runs on every push to `main` and on every pull request from a branch of
this repository: Windows with Visual Studio 2022 on a `windows-2022`
runner, Linux with Clang on `ubuntu-24.04` and, for ARM64, on
`ubuntu-24.04-arm` (the same recipe; `SNAP_TARGET_ARCH` names the package
`linux-arm64`, and the SSE flag on the audio microcode's source is x86's
only), each from a clean checkout plus `tools/fetch_deps.py` and the
private inputs of step 15, ending in cpack's archive as an artifact, with
the suite's package checks run on every archive. A fourth job builds the
Flatpak: `flatpak-builder` takes the same checkout as its one source
(`linux/flatpak/io.github.jackandbeans.Snap64Recomp.yml`), runs the same
CMake build inside the freedesktop 24.08 SDK with its LLVM 18 extension,
installs the flat folder into `/app/bin` (the program reads its shipped
files from beside itself, and writes to the sandbox's config folder since
`/app` is read-only, `src/paths.cpp`), adds the icon, the desktop entry and
the AppStream metainfo from that folder, bundles it as one `.flatpak` file
with a `.sha256`, installs the bundle and starts it with no ROM and no
window (`SNAP_ROM_PICK=cancel`, `SDL_VIDEODRIVER=dummy`): the program says
its name and its folder and exits. The x86_64 job's first run, on the
1.1.0 tree (2026-09-27), took ten minutes for Windows and six for Linux
and made archives with the release's file lists; run 36348338742 the same day made
the ARM64 tarball in two minutes and the Flatpak in nine. No game runs in the
workflow beyond that start. A pull request from a fork builds nothing
there: the inputs are private, so its jobs are skipped.

### 15. macOS build (GitHub's Mac)

I have no Mac, so this section is different in kind from the two above: it
describes a build made and run on GitHub's virtual Mac, not on a machine
of mine. The glue is the community's Apple Silicon build of 1.0.0 (pull
request #2, by appleforever11, which started, drew with Metal and reached a
course on an Apple M3 Pro), ported onto today's tree piece by piece behind
`__APPLE__` and `if (APPLE)`, so Windows and Linux compile exactly what they
did, and completed on 2026-09-26 with what Zelda64Recomp's macOS build
carries and what the port's own features need there. What the runner's
runs found and showed is at the end of this section.

What a Mac needs: macOS 14 or newer (the deployment target,
`CMAKE_OSX_DEPLOYMENT_TARGET` in `CMakeLists.txt`) on Apple Silicon or an
Intel Mac: the build is universal, one executable with both halves,
Xcode with its Metal compiler (the command-line tools alone have no `metal`;
Xcode 26 keeps the Metal toolchain as a separate download, `xcodebuild
-downloadComponent MetalToolchain`), CMake 3.20 or newer, Ninja and Python 3,
and the same generated inputs as every other build (steps 1 to 9:
`RecompiledFuncs/`, `RecompiledPatches/`, `patches/build/patches.bin` and the
ROM; the pull request made them on the Mac itself, with Homebrew's
`mips-linux-gnu-binutils` and clang as the decompilation's preprocessor).
Then, in the port root:

    python3 tools/fetch_deps.py
    python3 tools/hook_funcs.py
    cmake -S . -B build-macos -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" -DSNAP_ROM=/path/to/pokemonsnap.z64
    cmake --build build-macos --target Snap64Recomp
    python3 tools/macos_bundle.py build-macos

The last line installs the flat folder into `build-macos/bundle-stage`, lays
`build-macos/Snap64Recomp.app` out from it, writes its `Info.plist`, signs
it ad hoc with the hardened runtime and `tools/macos/entitlements.plist` (no
developer account and no notarization, which is how the other
recompilations ship too: the first launch is a right-click and Open, or on
macOS 15 System Settings > Privacy & Security > Open Anyway) and zips it as
`Snap64Recomp-<version>-macos-universal.zip` with a `.sha256` beside it and
a START HERE text for macOS inside. `-DCMAKE_OSX_ARCHITECTURES=arm64` alone
makes an Apple Silicon build half the size, named `-macos-arm64`.

What differs from the other two builds, all in `#if` branches of the same
files and `if (APPLE)` blocks of the same `CMakeLists.txt`:

* Rendering is Metal, through plume's Metal backend; RT64's build compiles
  its shaders to Metal libraries (`dxc-macos` to SPIR-V, RT64's
  `spirv_cross_msl` tool to MSL, Xcode's `metal` to a `.metallib`).
  `graphics_api` in the settings file is ignored
  (`src/rt64_render_context.cpp`). The Metal headers, metal-cpp, are plain
  files inside plume's tree, so `fetch_deps.py` needed no new pin.
* The window is created with `SDL_WINDOW_METAL`, and RT64 is handed the
  `NSWindow` and the `CAMetalLayer` SDL attaches to it (`src/main.cpp`,
  `create_window`), the pair ultramodern's `WindowHandle` holds on Apple.
  SDL's Metal view would also set that layer's size whenever the window
  changes, and plume's swap chain updates the size of the pictures it draws
  only when it finds the layer at another size than the window: after a
  resize or a switch to or from fullscreen it would have drawn at the old
  size into pictures of the new one. `src/macos_support.mm` turns SDL's
  update into nothing before SDL makes the view, as Zelda64Recomp does.
* The presented-frame capture and the Snap Station's sheet capture copy a
  texture into a buffer, which plume's Metal backend could not do (the copy
  fell through to the image-to-image path and dereferenced a null texture,
  as its Vulkan backend once did); the change is in `plume_metal.cpp` on
  the plume fork (VENDORING.md).
* The host threads that run game code get an 8 MB stack
  (`ultramodern::threads::make_game_host_thread` and
  `start_detached_game_host_thread`, in the runtime; VENDORING.md). Windows
  gives every thread the executable's `/STACK`, 8 MB, and Linux 8 MB, but
  macOS gives every thread but the main one 512 KB, a sixteenth of what the
  game has ever run on.
* A mod patches game functions inside the executable's own code, which macOS
  allows only when the code segment's maximum protection includes writing.
  ld64 will not link that, so `tools/macos/ld64` (clang's `-fuse-ld`) runs
  the real linker and then sets `__TEXT`'s `maxprot` to rwx in every image
  of the output, the approach of Zelda64Recomp's `.github/macos/ld64`
  without its macholib dependency; the edit breaks the signature ld64 put on
  each Apple Silicon image, so a post-build step signs the build tree's
  executable again, ad hoc. The bundle's entitlements (Zelda64Recomp's:
  JIT, unsigned executable memory, executable page protection off, library
  validation off) let the signed program patch itself and run a mod's
  recompiled code.
* SDL2 is built from the vendored tree, as on Windows, but static: the
  bundle then carries no dylib of its own to re-path or sign.
* The data directory is `~/Library/Application Support/Snap64 Recomp/`
  (`SDL_GetPrefPath`), always: a bundle's contents are sealed by its
  signature. The shipped, read-only files are read from the bundle's
  `Contents/Resources`, which is what `SDL_GetBasePath` returns to a bundled
  program and so what `snap::exe_dir()` is (`src/paths.cpp`). The ROM is
  looked for in the data directory, and the first run's chooser copies it
  there; `mods/`, `mods.json` and the mods' settings live there too.
* The log, the single-instance lock and the message-box deferral are the
  Linux ones (POSIX). The Snap Station's relaunch and the Mods page's Restart
  `execv` the image `_NSGetExecutablePath` names, and a folder opens with
  `open` (`src/snap_station.cpp`).
* The controller subsystem is started and stopped by the pad thread, not by
  `SDL_Init` on the main thread (`src/input.cpp`, `pad_thread_main`): SDL's
  IOKit driver binds its device matching to the run loop of the thread that
  initialized it and services that loop from `SDL_GameControllerUpdate`,
  which the pad thread calls; on any other thread no pad on that driver is
  ever seen. HIDAPI pads (Xbox, PlayStation, Switch Pro) do not depend on it
  either way.
* ld64 gets neither `--allow-multiple-definition` nor the group switches:
  under Clang the recompiler declares every game function weak
  (`RECOMP_FUNC`), so a patch's definition wins on its own. The `-msse4.1`
  flag on the audio microcode's source goes to the Intel half only
  (`-Xarch_x86_64`); on ARM the runtime's `sse2neon` serves.
* `lib/rt64/src/common/rt64_hlslpp.h` includes the C standard library before
  hlsl++, which the pull request needed under Apple's libc++ (one more file
  in the RT64 fork's commit).

The workflow, `.github/workflows/macos.yml`, runs the commands above on
GitHub's `macos-15` runner (Apple Silicon, a virtual Mac whose GPU is Apple's
paravirtual one: Metal works there for correctness, not for speed) when it
is started by hand, and checks what it made: both halves in the executable
and each one's `__TEXT` writable at most (`otool -l`), the signature and its
entitlements, and then the bundle itself with `tools/macos_smoke.py`, which
runs it as a player's first start does (a fresh data folder, `SNAP_DATA_DIR`)
and reads what it prints: the title screen from a cold start with frames
captured and lit, the same boot under Metal's API validation
(`MTL_DEBUG_LAYER`), the release suite's scoring replay (45 photos in the
healthy signature, compared line by line with the Windows build's scores in
`tools/replays/eval.scores`; a difference is reported, not failed) and a mod
whose hooks run (Snap64RecompMods' `zz_hooktest`). The Intel half is run the
same way through Rosetta 2, the only way an Intel Mac's code path runs on
that machine. The script was run against the Windows build before it ever
ran on a Mac (with the Metal line failing, as it must there): 45 of 45 score
lines identical to the reference, the mod's hooks fired, three lit frames.
The score stage passes on the lines scored (twenty or more, all inside the
healthy signature) and reports how many photos stayed in step with Windows:
a tape of presses per pad reading holds only while the machine keeps the
console's pace at every press, and a shared virtual Mac does not always
(`SNAP_TICK_DELAY_MS`, the port's switch that makes every tick longer,
stops the same tape in the lab on a PC). `tools/macos_timer_probe.py`
runs first and says how precisely the machine can wait one retrace.
The inputs the ROM produces come from a private repository,
`JackandBeans/Snap64RecompInputs` (`RecompiledFuncs/`, `RecompiledPatches/`,
`patches/build/patches.bin`, `pokemonsnap.z64`, the replay's save and the
test mod, laid out by `tools/ci_inputs.py` from the tree the workflow
builds), cloned with a fine-grained token limited to that repository's
contents and kept as this repository's `SNAP64_INPUTS_TOKEN` secret; that is
what the other recompilations' workflows do with their own private
repositories. `tools/ci_inputs.py` also writes `inputs.json`, a fingerprint
of the tracked files the recompiled code is made from (`pokemonsnap.us.toml`,
`patches.toml`, `patches/`); every workflow checks it against its own tree
before building and stops when the inputs were made from another, which
would have built that tree's patches. The job never runs for a pull request, so it never needs a
fork's secrets, and without the secret it stops at that step. It uploads the
zip with its checksum, and every check's log, frames and any crash report
macOS wrote, whether the checks passed or not.

What GitHub's Mac showed (2026-09-26 and 27, macOS 15.7, Xcode 16.4, a
`VirtualMac2,1` with three CPUs and Apple's paravirtual GPU): the universal
executable links, both halves carry the writable code segment, the bundle
signs with the entitlements and verifies after the zip; the game boots to
the intro at 57 frames a second, natively and under Rosetta 2, with three
captured frames showing the Nintendo logo and the port's name card; Metal's
API validation reports no failed assertion; the Beach ride scores its
photos inside the healthy signature, within a few units of the Windows
numbers; the hook test mod loads and its hooks run; the program quits
without a crash report. Three faults the runs found are fixed in the tree:
Apple's paravirtual GPU driver failed an assertion inside
`sampleCountersInBuffer` after saying it supported the sampling point
(plume's Metal backend takes no timestamp counter set from a device named
Paravirtual, or under `RT64_NO_GPU_TIMESTAMPS`); the runtime's detached
threads locked destroyed mutexes at exit (their objects are never
destroyed now); and the virtual machine woke a plain 16.7 ms sleep 61 ms
late, so the game ran at 13 retraces a second (the VI and timer threads
take macOS's time-constraint scheduling policy and wait with
`mach_wait_until`, measured at 0.04 ms). No physical Mac has run the
build.

### 16. Android cross build (experimental)

The whole port links as an Android shared library from this tree with the
NDK, for arm64-v8a (devices) and x86_64 (the emulator): `libmain.so`, which
SDL's own activity loads after `libSDL2.so` and enters through `SDL_main`
(`SDL_main.h` renames `main()` to it there and exports it; the Windows build
is entered the same way through SDL2main, a Linux binary keeps its `main`).
The app around the library is described under **The app** below, and what
has run where, with dates, under **What has run where** at the end of this
step. The link was first recorded on 2026-10-19 under WSL (Ubuntu 24.04) with Android NDK r30
(30.0.16248370, its Clang 21.0.0) unpacked under the home folder, CMake 3.28
and Ninja, in the same copy of the port root on the Linux filesystem as
step 14, whose `build-linux` supplies the two programs a cross build cannot
run. In that copy:

    NDK=$HOME/android-ndk-r30
    cmake -S . -B build-android-arm64 -G Ninja -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release -DSNAP_ROM=/path/to/pokemonsnap.z64 -DSNAP_ASPMAIN_PREBUILT=$PWD/build-linux/rsp/aspMain.cpp -DRT64_FILE_TO_C_HOST=$PWD/build-linux/lib/rt64/src/tools/file_to_c/file_to_c
    cmake --build build-android-arm64 --target Snap64Recomp -j"$(nproc)"

and the same with `-DANDROID_ABI=x86_64` in `build-android-x86_64`. The
two variables are required when cross-compiling, because the build cannot
run the `RSPRecomp` and `file_to_c` it makes: `SNAP_ASPMAIN_PREBUILT` names
the `rsp/aspMain.cpp` a native build of this tree generated from the same
ROM (step 9) and `RT64_FILE_TO_C_HOST` that build's `file_to_c`
(`lib/rt64/CMakeLists.txt`, which also chooses the shader compiler, dxc, by
the host system now). API 29 (Android 10) is the floor chosen for now:
plume asks for a Vulkan 1.2 instance, which is that era's anyway;
`c++_static` is the NDK's static C++ runtime inside each library. Both are
one `-D` to change.

What differs from the Linux build, all in `if (ANDROID)` and
`#if defined(__ANDROID__)` branches of the same files (Android defines
`__linux__` too, so what is a Linux desktop's alone now sits under
`__linux__ && !__ANDROID__`):

* SDL2 is built from the vendored tree as a shared library, and the game
  target is a shared library named `main`; no `-static-libstdc++`, no
  `pthread` (bionic has it), `dl` for `dlopen`. Nothing is placed beside the
  library, and there is no install tree and no cpack: those files become
  the package's assets.
* RT64: no nativefiledialog-extended, `texture_hasher` or `texture_packer`;
  the SDL window path (`RT64_SDL_WINDOW_VULKAN`, plume's
  `PLUME_SDL_VULKAN_ENABLED`) holds for a system name of Android as for
  Linux, and plume's own Android arms (a native window,
  `vkCreateAndroidSurfaceKHR`) step aside for it; zstd's dictionary builder,
  which nothing uses, is left out (it calls `qsort_r`, which bionic lacks at
  API 29). VENDORING.md lists the files.
* `src/paths.cpp`: the data directory and the shipped files' folder are the
  one game folder the app's launcher names through `SNAP_DATA_DIR` (Snap64
  at the root of the shared storage, below); without it, the app's own
  folder on the shared storage (`SDL_AndroidGetExternalStoragePath`), which
  needs no permission. `SDL_GetBasePath` is not supported on Android.
* `src/rom_picker.cpp`, `src/mod_installer.cpp`: the file chooser is the
  system's document picker, reached through the activity
  (`src/android.cpp`); the picked document arrives as a copy in the app's
  cache, which is removed once examined. `SNAP_ROM_PICK` and
  `SNAP_PICK_TEST` work as on every platform.
* `src/snap_station.cpp`: the relaunch is logged as unavailable (an app
  cannot replace its own process image), and the sheet's folder is not
  opened.
* `src/steam_deck.cpp`, `src/main.cpp`: no Deck, gamescope, X11, RADV, icon
  or desktop entry. The game starts fullscreen with the system bars hidden.
  The log is the Linux one, in the game folder; the single-instance lock
  is `snap64.lock` in the app's internal storage
  (`SDL_AndroidGetInternalStoragePath`), because the shared storage refuses
  file locks.
* `src/touch_controls.cpp` with `Snap64TouchOverlay.java`: the touch
  controls, nine buttons in the bars beside the 4:3 picture (A, B and Start
  always; Z, R and the four C buttons in a course), colored like the N64
  pad. In a course a drag aims through the mouse's path; outside a course,
  and on a menu that takes the mouse, a tap is a click. Each press is held
  until the game has read it, so quick taps all arrive. With
  `touch_controls` at 0 (automatic) the buttons hide when a pad or a
  keyboard is used and come back at the next touch; 1 shows them always, 2
  never. `touch_size` (50 to 150 percent) and `touch_opacity` (20 to 100)
  are in `snapsettings.json` only: there are no Options rows for them yet.
  The buttons keep clear of the system's back gesture and of a camera
  cutout.
* Leaving the screen (Home, the recent apps, the screen turned off): the
  runtime holds the VI retraces and the console's clock while the app is
  away (`lib/N64ModernRuntime`, ultramodern), so the game waits and its time
  does not jump; plume makes the Vulkan surface again from SDL's new window
  on return (`lib/rt64/src/contrib/plume`). A touch button held as the app
  leaves is let go.
* `src/gpu_check.cpp`, the Android half of the PC's adapter check: before
  the renderer is made, every Vulkan device is named in the log
  (`[SNAP-VK] device: <name>, <type>, Vulkan <version>, driver <number>,
  vendor <id>, swap chain yes or no`), and the one plume will take is
  refused, with a dialog saying why, when it offers less than Vulkan 1.2
  or no swap chain; plume makes a 1.2 instance and device, and such a
  device would otherwise fail inside the renderer with nothing said.
  `SNAP_VK_TEST=1.1` has the device answer 1.1, to see the refusal on a
  device that would pass. A fatal box is never put off to the main loop's
  next pass (`src/main.cpp`, on every platform): the renderer's refusal
  came from its own thread after that pass had begun deferring boxes, and
  the process ended with the box still queued.
* `src/android_thermal.cpp`: a `[SNAP-THERMAL]` line in the log every
  minute and at each change of Android's thermal status, with the headroom
  Android forecasts ten seconds ahead (the status from Android 11, the
  headroom from 12; both are looked up at run time).
* `src/main.cpp`: SDL's hint that lists the accelerometer as a joystick is
  off, so a phone's accelerometer is never taken for a pad (it had filled a
  phone's log with a line per poll).
* `src/input.cpp`, on every platform: an open pad with no stick gives way
  to one with a stick when the list of joysticks changes, and among the
  pads attached at the start one with a stick is opened first. SDL maps
  the Android emulator's own keyboard (`qwerty2`) as a pad with A, B,
  Start and a D-pad; it was opened first and kept, so a real pad attached
  later was never used. A joystick SDL cannot map is named in the log once,
  not at every poll of the pad thread.
* `lib/N64ModernRuntime/librecomp/src/mods.cpp`: a mod's patch of a game
  function asks for a page that is readable, writable and executable at
  once (VENDORING.md says why), and on ARM64 the instruction cache is
  synchronized after every patch is written or removed
  (`__builtin___clear_cache`). Proved on 2026-10-21 by
  Snap64RecompMods' `zz_hooktest`, on the emulator and on the arm64 phone.

Recorded on 2026-10-19, from the tree at this commit: both links
succeed. arm64-v8a: `libmain.so` of 106,938,192 bytes as built (the NDK
compiles everything with debug information; 11,405,184 after
`llvm-strip --strip-unneeded`), an `ELF64` `AArch64` shared object that
exports `SDL_main` (`llvm-nm -D`) and needs `libdl.so`, `libSDL2.so`,
`libm.so` and `libc.so`, beside a `libSDL2.so` of 5,959,688 bytes. x86_64:
`libmain.so` of 101,770,088 bytes (12,096,232 stripped) and `libSDL2.so` of
6,278,816, the same symbol and the same four libraries. The compiler's
warnings (92 and 75) are all in files every platform compiles (the RSP
vector header, stb, sse2neon, the deprecated codecvt, miniz), none in the
Android branches. Afterward `build-linux` reconfigured and relinked its
`Snap64Recomp` from the same tree, the proof the Linux build is untouched;
the Windows build was not rebuilt for this.

**The app.** `android/` is the Android project: SDL's own `android-project`
reshaped for the port, with the versions that tree ships (Gradle 8.1.1 and
the Android Gradle plugin 8.1.1, on a JDK 17). It compiles nothing of the
port: `android/app/build.gradle` takes `libmain.so` and `libSDL2.so` from
the two cross builds above (and stops with a message when a build is
missing), reads SDL's Java activity from `lib/SDL/android-project` in place,
stages the files the port ships beside its executable (`menu_text/`, the
seen-shader list, `gamecontrollerdb.txt`, the two README files, and since
2026-10-22 `LICENSE`, `NOTICE.md` and the three licenses the Linux archive
carries) as the package's assets, in a folder of their own, `snap64/` (the
root of an app's assets also lists Android's own folders, `images`,
`webkit` and `geoid_map`, which the first start used to copy into the game
folder with them), and takes the version from `CMakeLists.txt`. The port's
own Java is three classes in
`android/app/src/main/java/io/github/jackandbeans/snap64recomp/`:

* `Snap64Launcher`, the first screen. It settles the game folder: Snap64 at
  the root of the shared storage (Internal storage > Snap64 in a file
  manager, the folder a PC shows over USB), the one folder holding the ROM,
  the settings, the saves, `mods/`, `texture_packs/`, the photos and
  `snap64.log`, as the port's own folder does on a PC. That takes all-files
  access on Android 11 and later, which only the player can grant, in the
  system settings; the launcher asks once and sends the player there, and
  without the access the game folder is the app's own,
  `Android/data/io.github.jackandbeans.snap64recomp/files`, which needs
  none. A document the app was opened with (a mod, a texture pack, a zip
  holding them, the ROM: the manifest's "open with" filters) is copied
  into the app's cache, since the permission to read it ends with the
  launcher, and every intent extra is forwarded.
* `Snap64Activity`, SDL's activity. It hands the folder and the extras to
  the native side as environment variables before the game starts
  (`SNAP_DATA_DIR`, the port's own switch; and whatever adb passed, with
  the same `SNAP_*` names as on a PC), copies the shipped files from the
  assets into the game folder when they are missing, and shows the system's
  document picker when the native ROM chooser or the Mods page's Install
  row asks for one through JNI (`src/android.cpp`, which also feeds the
  log's lines to logcat under the tag `Snap64`). The Back button reaches
  the game as the `AC_BACK` key and opens the quit box; the file the app was
  opened with is dropped on the window at the first frame (`SNAP_OPEN_FILE`,
  `src/main.cpp`), which installs it as a drop does on a PC. As the app
  leaves the screen it lets go of any touch button still held. (The game's
  own wait while the app is away is SDL's app events, read by an event
  filter in `src/main.cpp`.)
* `Snap64TouchOverlay`, the touch controls' buttons, one small view each
  over SDL's surface, so a touch anywhere else still reaches the game: in
  the black bars beside the 4:3 picture on a wide screen, over its edges on
  a narrower one. The native side (`src/touch_controls.cpp`) says every
  tenth of a second whether they show, whether a course runs, and their
  size and opacity; the overlay sends it the buttons held at every change.
  Each visible button asks the system to keep its back gesture off that
  spot.

The manifest keeps the game in landscape (SDL's orientation hint names the
two landscape sides, because SDL asks for the player's own rotation for a
resizable window), lists the configuration changes the game handles itself
(a change of display size or font size included, which would otherwise end
SDL's game thread), and marks the app as a game: `res/xml/game_mode_config.xml`
lets the phone's game tools neither draw the game smaller nor change its
frame rate, and offers no battery or performance mode, because the port has
none. The launcher icon is adaptive: `tools/make_android_icon.py` makes its
layers from the port's own icon (`src/snap64.icns`, unchanged), with a
lettering layer for themed icons.

The build, with the SDK under the home folder (a JDK 17, the command-line
tools, platform 35, build-tools 35.0.0 and platform-tools), from the
port root:

    export JAVA_HOME=/path/to/jdk-17 ANDROID_HOME=/path/to/android-sdk ANDROID_NDK_HOME=/path/to/android-ndk-r30
    cd android && ./gradlew assembleDebug

writes `android/app/build/outputs/apk/debug/app-debug.apk`, signed with
the debug key, carrying both ABIs. `ANDROID_NDK_HOME` names the NDK of the
cross builds to the plugin, which strips the libraries with its
`llvm-strip`; without it they go in with their debug information, a
hundred megabytes each. adb drives it the way the suite drives a
PC build: `adb shell appops set io.github.jackandbeans.snap64recomp
MANAGE_EXTERNAL_STORAGE allow` once (the access without the screen), then
`adb shell am start -n io.github.jackandbeans.snap64recomp/.Snap64Launcher
-e SNAP_DATA_DIR /sdcard/Snap64 -e SNAP_REPLAY /sdcard/Snap64/beach.inputs`
and so on; the log is `snap64.log` in the game folder, and `adb logcat -s
Snap64` shows its lines as they are written.

The release build is `./gradlew assembleRelease`, which writes
`android/app/build/outputs/apk/release/Snap64Recomp-<version>-android.apk`
and its `.sha256` sidecar (`<sha256>  <name>`, one line ending in LF), as
CPack names the other archives. The version is `CMakeLists.txt`'s, with a
prerelease label from `-PsnapPrerelease=rc1` or `SNAP_VERSION_PRERELEASE`
(the cross builds must be configured with the same label). It is signed only
when the environment names the release key; the key never enters the tree
(`*.jks` and `*.keystore` are ignored):

* `SNAP64_ANDROID_KEYSTORE`: the keystore file's path, or the file itself
  in base64 (as a CI secret holds it; the build writes it to a temporary
  file outside the tree, removed when Gradle's JVM ends);
* `SNAP64_ANDROID_KEYSTORE_PASSWORD` and `SNAP64_ANDROID_KEY_ALIAS`;
* `SNAP64_ANDROID_KEY_PASSWORD`, only when the key's password is not the
  store's.

Without the three, the release APK is built unsigned and named
`Snap64Recomp-<version>-android-unsigned.apk`, which Android will not
install. `app/build.gradle` asks for the v2 and v3 schemes; at this
minimum version the plugin signs with v3 alone (`apksigner verify`), which
every Android from 9 on reads, and v3 lets the key be rotated later
without breaking updates. The key is made once, with the JDK's `keytool`, and kept
by its owner with a copy away from the build machine:

    keytool -genkeypair -v -keystore snap64-release.jks -alias snap64 -keyalg RSA -keysize 4096 -validity 36500

Every later APK must be signed with the same key: Android installs an
update only over an app signed with the same certificate, so a lost key
means every player uninstalls to update. `python tools/release_check.py
--only package --zip <apk>` checks a release APK: both ABIs' libraries,
16 KB-aligned and for the right machine; the assets and the licenses; the
manifest's package, version (the file's name and the libraries say the
same) and no debuggable app; no ROM; no build machine's paths; the sidecar;
and a signature that is not the Android debug key's.

Recorded on 2026-10-22, from the tree at this commit, under WSL: the debug
APK (11,697,873 bytes) and the unsigned release APK (10,612,051 bytes);
then the release APK signed with a throwaway key made for the test and
deleted after, given once as a path and once in base64 (10,620,243 bytes
each, `apksigner verify`: verifies, scheme v3, the throwaway certificate;
no temporary copy of the key left behind). A value that is neither a file
nor a keystore stops the build with a message, and so does a label with a
capital letter; `-PsnapPrerelease=rc1` names the APK
`Snap64Recomp-1.1.6-rc1-android-unsigned.apk`. The package check passed
every check on the signed APK and failed only "NOT signed" on the unsigned
one; on copies made to fail, it failed the debug APK as debuggable and
debug-signed, an APK under another version's name on the manifest and the
libraries' version, and one with an N64 header added inside on the ROM
(and on its broken signature). The signed release APK, installed on the
emulator in place of the debug one, passed the boot, mod, settings and
rompick stages, 17 of 17.

`tools/android/build.sh` makes the whole app from a prepared port root
(the vendored trees fetched, the inputs the ROM produces in place): a
native build of the two host tools alone (`file_to_c` and the audio
microcode's `rsp/aspMain.cpp`) in `build-android-host`, the two cross
builds, and the release APK. It takes the variables above, plus
`SNAP_VERSION_PRERELEASE`, and `SNAP_LAUNCHER=ccache` for a compiler
cache. `.github/workflows/build.yml`'s Android job runs it on GitHub's
Ubuntu runner with NDK 30.0.16248370 from the runner's `sdkmanager`, then
the package check (with `--unsigned-apk` when the key's secrets do not
exist), the build provenance for a release, and the APK and its sidecar as
the artifact `Snap64Recomp-android`. Without the three secrets a release's
APK is built unsigned as well, and the release machine signs it with
`tools/release/sign_apk.sh` ("To cut a release", step 4).

Recorded on 2026-10-22, as far as this machine can: the job's commands
from a fresh clone of the branch under WSL, with two stand-ins, since
GitHub does not have the forks' Android commits yet and the private inputs
are cloned with a secret: the vendored trees copied in, and the inputs laid
out from the same generated code with `tools/ci_inputs.py`. From the
inputs' check (`--check`: "the inputs belong to this tree") on, every
command was the job's. The script built the host tools, both libraries and
the APK in 292 s (32 cores, no compiler cache), and the unsigned APK
passed the package check, 14 of 14 with `--unsigned-apk`, no build
machine's path in it. The fresh clone found two faults the long-lived copy
had hidden: a Windows checkout gave `android/gradlew` CRLF line endings,
which stopped Gradle under WSL (now LF by `.gitattributes`), and git held
it without its execute bit, which stopped the script on Linux (now
executable). The job itself runs for the first time on GitHub's machines.

Recorded on 2026-10-19: the debug APK built from this tree (`BUILD
SUCCESSFUL`; the plugin fetched its own build-tools 33.0.1 beside the 35.0.0
installed, under the license accepted once). `app-debug.apk` of 26,611,382
bytes, version 1.1.6 (code 10106), API 29 to 35, native code arm64-v8a
and x86_64 with the libraries stripped (`libmain.so` 11,411,072 and
12,102,968 bytes, `libSDL2.so` 1,291,856 and 1,415,376), the seven
assets, `Snap64Launcher` as the launchable activity.

**The first run, 2026-10-20**, on Google's x86_64 emulator (37.2.12, the
Android 15 Google APIs image, a pixel_6 profile, no window, the host's
GPU through the emulator's gfxstream), driven by adb the way above: the
Mac smoke's boot stage (`SNAP_MUTE`, `SNAP_ROM_PICK=cancel`,
`SNAP_PCAP_ATFRAME=300,900,1500`, `SNAP_PCAP_BURST=1`, `SNAP_STATS=1` as
intent extras, `SNAP_DATA_DIR=/sdcard/Snap64` with the ROM pushed there).
snap64.log in that folder: the version line, "data directory:
/sdcard/Snap64/", the ROM accepted, "graphics API: Vulkan", "Device Name:
AMD Radeon RX 9060 XT", the display paced at 60 Hz, "surface is
1080x2400", the game's own ticks at 30 a second, and "[SNAP-PCAP] wrote"
at game frames 301, 901 and 1501: three 1600x1200 frames in
`snap_frame_dumps/` with a picture on each (mean luminance 54, 122, 122),
116 s from the start to the third, the app alive when stopped, logcat's
crash buffer empty. Three runs before it taught what the tree now has:
the launcher asks no question when adb names the folder; the activity
hands that folder on (it had skipped it); rt64 names one swap chain
format, RGBA on Android (plume takes only the format asked, and every
`vkCreateSwapchainKHR` failed on the desktop's BGRA); and the process
enters its game folder at start, since an app's working directory is "/"
and the captures' relative path went nowhere. Not run that day: a device,
the arm64-v8a library, a pad, the picker, the Back key, a mod (all but the
pad have run since; below). The emulator starts windowless (`-no-window`)
so a run shows nothing on the desk, and the "all files" access is granted
to the package with `appops set --uid` as well as the plain form.

Since 2026-10-21 the native libraries are compressed in the package and
unpacked when it installs (`useLegacyPackaging` in `app/build.gradle`): this
plugin version stores uncompressed libraries at 4 KB boundaries, and
Android 15's 16 KB-page image refused to install the app. The libraries' own
segments are 16 KB-aligned (NDK r28 and later). The debug APK of 2026-10-21
is 11,668,462 bytes.

`tools/android_check.py` drives the app through adb on an emulator or a
test lab's phone, stage by stage, by the rules the PC suite uses; its
docstring lists the stages. Its pad stage needs a pad that is not there:
`tools/android/uipad.c` makes one through Linux's uinput, which takes root,
so on an emulator only. Android takes it for a game pad (it carries an Xbox
360 controller's ids, whose key layout every Android ships), and SDL opens
it as it would a real one. It is built with the NDK and handed to the
check:

    $NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/x86_64-linux-android29-clang -O2 -o uipad tools/android/uipad.c
    python tools/android_check.py ... --uipad uipad --stages pad

**What has run where.** Every run below is the app built from this tree
on the day named.

* Google's x86_64 emulator (Android 15 Google APIs image, a pixel_6
  profile, 2400x1080 in landscape, the host's GPU through gfxstream),
  2026-10-21, the tree with the touch controls and the lifecycle work:
  `tools/android_check.py`, every stage, 48 of 49 checks passed. The one
  failure was the boot stage's own check, which took the touch buttons'
  lettering for the system's status bar; corrected in the tool, that stage
  then passed 7 of 7. By stage: boot 7, mod 3, lifecycle 6, touch 11, score
  3, stats 5, attract 2, menu 2, pages 4, settings 3, rompick 3. The touch
  stage passed 11 of 11 with widescreen on as well. In the lifecycle
  stage's events the game's clock moved 0.0 s while the app was away; before
  the clock was held, 14.0 s away moved it 14.0 s. The emulator draws slower
  than a phone, so the score stage was in step with Windows for 10 of the
  reference's 20 photos; its pass rule is the healthy signature on every
  scored line, which held. The Android-only paths
  (the all-files dialog, the ROM through the system's picker, Back's quit
  box, "Open with" for a mod while the game runs and from cold) passed 5 of
  5 with a uiautomator script, in the tree since 2026-10-22 as
  `tools/android/paths_check.py`.
* The same emulator's Android 15 image with 16 KB memory pages,
  2026-10-21, the tree before the touch controls: the boot and mod stages,
  10 of 10.
* A Samsung Galaxy S25 Ultra (SM-S938U: Snapdragon 8 Elite, Adreno 830,
  Vulkan driver 0x80320023, Android 15, 2340x1080 at 120 Hz) in Samsung's
  Remote Test Lab, 2026-10-21, the tree before the touch controls and the
  lifecycle work: the boot, mod, score, stats, attract, menu, pages,
  settings and rompick stages all passed. The score stage gave 37 scored
  lines in 191 s, none outside the healthy signature, in step with Windows
  for 15 of the reference's 20 photos. The stats stage passed with the
  suite's frame rate setting (`fps_mode` 1); with a fresh folder's default
  (0) the game prints fewer pacing reports and no coherence lines, so the
  check's counts fail, which is the setting, not a fault. The mod's hooks
  ran on ARM64.

* Google's emulator, 2026-10-22, the tree with the pad fix above: the pad
  stage, 8 of 8, with a pad made through uinput. It was opened in place of
  the emulator's keyboard; Start went through the intro; on the recorded
  tape each of 18 presses (the shoulders, the triggers, B and X, the D-pad,
  the right stick's four C buttons, the left stick's four directions, A)
  was read as the N64 input the default bindings give it; the touch
  controls stayed while the pad was only attached, hid at its first press
  and came back at a touch; the right shoulder fast-forwarded, the left
  stick pressed in slowed the game (with slow motion turned on), and Back
  opened the pages. The same stage on the APK of 2026-10-21, before the
  fix, failed 3 of 8: the emulator's keyboard stayed the pad, and after the
  intro the tape held only a Start and an A (which reached the game some
  other way) and none of the other 16 presses.

* Google's emulator, 2026-10-22, the tree with the Vulkan check: the
  vkcheck stage, 4 of 4. With `SNAP_VK_TEST=1.1` the log said "[SNAP-VK]
  refused: ... offers Vulkan 1.1.0 and the renderer needs 1.2", Android's
  own dialog showed the message (read off the screen with uiautomator),
  the app waited on it, ended as soon as its OK was pressed, and made no
  renderer. The boot stage of the same build named the device (the host's
  GPU through gfxstream, Vulkan 1.3.0) and did not refuse it. Before the
  fatal-box fix, the same refusal ended the app 150 ms after its log line,
  with no dialog.

* Google's emulator, 2026-10-22, the final tree of the night (the debug
  APK of both ABIs): `tools/android_check.py`, every stage, 61 of 62. The
  one failure was the stats stage's count of coherence lines, 99 where the
  check asked for 500. Run alone it gave 244, and last night's APK, which
  had given 572, gave 302 in the same emulator session: the count follows
  the emulator's speed (its game ran at 13 to 16 frames a second that
  hour), so the stage now asks an emulator only for the lines to be there
  and holds a phone to the PC's 500. The Android-only paths passed 5 of 5
  on the same APK from a fresh install, and the unsigned release APK
  passed the package check, 14 of 14.

Not run anywhere yet: a real pad, wired or Bluetooth; the Button Setup
page's capture of a press on Android (its code is shared with the PC);
the sound, heard by a person;
the touch controls on a real screen; the touch controls and the lifecycle
work on a real device; any GPU but the Adreno 830 on a real device (Mali,
PowerVR and older Adreno chips are untried); Android 10 to 14 (the app's
floor is Android 10, and both the emulator and the phone ran Android 15).

## What a clean checkout is missing

A `git clone` of this repository today contains the port's sources, the
submodules' records (which commit of which repository each vendored tree
is), the recompiler configs, the tools, the patch sources, the microcode
template `rsp/aspMain.us.toml.in` and the two symbol files. It does **not**
contain:

1. The vendored trees themselves -- `python tools/fetch_deps.py` checks the
   33 submodules out at their recorded commits (step 10). CMake cannot
   configure without them.
2. `RecompiledFuncs/` and `RecompiledPatches/` -- generated (steps 4-5, 8);
   generating them needs the ROM, the decomp build and N64Recomp under WSL.
3. `pokemonsnap.relocs.elf` and `pokemonsnap.z64` in the port root -- the
   recompiler's inputs (step 3); the ROM is also what CMake recompiles the
   audio microcode from (step 9, `SNAP_ROM`), so it is needed even when the
   recompiled code is copied in rather than generated.
4. The decomp, IDO, the MIPS binutils and N64Recomp themselves.

Until 2026-09-27 RT64 and the runtime were tracked copies and the rest were
ignored directories fetched at recorded pins; VENDORING.md has that history.

Four workflows run on GitHub. `.github/workflows/docs.yml` runs
`tools/check_docs.py`, which follows every relative link and anchor in the
Markdown files, and compiles the Python tools, on every push.
`.github/workflows/build.yml` builds the Windows and Linux archives (step 14)
and the Android APK (step 16, through `tools/android/build.sh`).
`.github/workflows/release.yml` makes a release's archives through the
other two and signs them (step 13, "To cut a release").
`.github/workflows/macos.yml` builds the macOS bundle (step 15): the ROM and
what is made from it are build inputs no public workflow may hold, so it
takes them from a private repository through a secret, as the other
recompilations' workflows do, and runs only for pushes to this repository.
The headless suite, `tools/release_check.py`, is described under
[Replays and the headless suite](#replays-and-the-headless-suite).
Packaging is `cpack` (step 13): the
`install()` rules lay out the portable folder, and the build stages the DLLs
and the `menu_text` badge beside the executable (step 12). The ROM is the one
file still placed by hand.

### The clean-checkout build, as verified

This record is of the layout before 2026-09-27, when RT64 and the runtime
were tracked copies and the other trees were fetched at recorded pins; the
submodule layout's proof is in VENDORING.md ("Submodules, on forks") and in
the build workflow's runs, which start from a clean clone every time.

On 2026-09-02, on the machine above, the list was tested end to end: a plain
local `git clone` into a second directory, then in it

    python tools/fetch_deps.py
    # copy RecompiledFuncs/, RecompiledPatches/ and pokemonsnap.z64 from the
    # working tree (the WSL outputs of steps 3-5 and 8; the copies were already
    # hooked, so tools/hook_funcs.py was not needed)
    cmake -S . -B build -G "Visual Studio 16 2019" -A x64
    cmake --build build --config Release --target Snap64Recomp --parallel

`fetch_deps.py` fetched all 23 trees in about two and a half minutes (its
second run, three seconds, reported every one already at its pin), configure
took 41 s and found the ROM and the patches, and the build took 4 min 4 s with
no errors, 10 compiler warnings and the linker's `LNK4088` (the port links
with `/FORCE:MULTIPLE`, `CMakeLists.txt`; the `/IGNORE:4088` beside it does
not silence that one). `build/Release` held `Snap64Recomp.exe` (10,083,840 bytes; the
version resource read `1.0.0-rc1`, the number of the day; the release is
`1.0.0`), `SDL2.dll`, `dxcompiler.dll` and
`dxil.dll` (the last two byte-identical to `lib/rt64/src/contrib/dxc/bin/x64/`),
`Snap64Recomp.map` and `menu_text/recomp_logo.png`. The fetched trees were
diffed against my own: identical apart from zstd's two test-suite
symlinks (empty files in the original checkout, link-target text in the new
one). The executable was not run as part of this check.

That record predates two changes made the same day: the two plume headers
became tracked (so `fetch_deps.py` no longer patches them), and `dxil.dll`
became the v1.7.2308 file (`VENDORING.md`, "dxc"). With the new validator in
place my build directory was rebuilt with its 53 compiled shaders
deleted first: all 53 were regenerated and signed through it, the executable
relinked, and `Release/dxil.dll` restaged (SHA-256 `9cccc7ef…`). The
v1.8.2403.2 validator had been tried first and refused the very first library
shader (`RasterPSLibrary.hlsl`, "Container part 'Runtime Data (RDAT)' does
not match expected for module"), which is why the validator stays in the
compiler's 1.7 series. The game was then run for 75 s on the Beach replay
with an empty shader cache, so that every raster shader it links at run time
through `dxcompiler.dll` was signed by the new `dxil.dll`: it presented in
step with the display throughout, wrote a 1.5 MB shader cache, and logged no
compiler, linker or pipeline error.

The proof was then repeated with the committed script (commit `aaa74a4`), at
23:00 the same day: a plain `git clone` (1 s; the three plume files came with
it), `python tools/fetch_deps.py` fetched all 23 trees in 174 s, replaced
`dxil.dll` with the v1.7.2308 file and restored the three plume files from
the repository, a second run reported every tree at its pin in 5 s, and after
`RecompiledFuncs/`, `RecompiledPatches/` and the ROM were copied in,
configure took 41 s and the Release build 244 s with no errors, leaving
`Snap64Recomp.exe` (10,083,840 bytes) with `SDL2.dll`, `dxcompiler.dll` and
`dxil.dll` (SHA-256 `9cccc7ef…`) beside it and the 53 shaders compiled. The
clone was deleted afterward.

## Replays and the headless suite

The port replays controller readings from `.inputs` files beside the
executable (`SNAP_REPLAY=name.inputs`; twelve bytes per reading, `src/input.cpp`).
Three are used by `tools/release_check.py` and are tracked under
`tools/replays/` (the suite copies them beside the executable when they are
not already there): `beach.inputs`
and `eval.inputs` were recorded by me (a Beach ride; a ride, the
Camera Check and Oak's evaluation of five photos), and `station.inputs` was
synthesized from those two on 2026-09-03 for the Snap Station: the evaluation
replay, a second Beach ride, an Album Mark in the Camera Check, Oak's check,
the lab's Save, the title menu's Gallery entry, four rows down and Print. The
suite's default run takes about thirteen minutes; `--only station` adds the
eight-minute print and puts the save and settings back afterward. All of them need
the ROM beside the executable, and they open the game window; a hidden window
starves the pacing numbers, so leave it on top.
