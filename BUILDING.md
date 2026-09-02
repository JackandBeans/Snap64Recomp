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

Nothing below has been exercised on Linux or macOS. `CMakeLists.txt` has
non-MSVC branches; no build on another platform is recorded in this repository.

## Prerequisites

### Windows

* Visual Studio 2019 with the C++ workload (the recorded build used MSVC
  19.29.30159 through the `Visual Studio 16 2019` generator, platform `x64`).
* CMake 3.20 or newer (the recorded build used 4.4.2).
* Python 3 (any recent version; the tools use only the standard library).

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
`patches/render-matrix-tagging.patch` for the record of exactly that mistake.

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
generated definition to `__real_<name>` and defining `<name>` itself
(`src/overlay_hook.cpp`, `src/matrix_tags.cpp`, ...). The script edits
`RecompiledFuncs/funcs_*.c` and `funcs.h` in place, is idempotent, and also
inserts the inner hooks listed in its `INNER_HOOKS` table. Run it after every
step 4.

### 6. Regenerate the overlay table (only when step 4 changed anything)

    python3 tools/gen_overlays.py pokemonsnap.relocs.elf

Writes `src/recomp_overlays.inl`, which is tracked. The table is wider than
N64Recomp's own (every ALLOC section of the ELF, data and bss included, with
`.index` equal to the ELF section header index) and drops librecomp's
`*_recomp` reimplementations from the function arrays; the tool's docstring
lists every rule. Against the recompiler output in this tree the tool
reproduces the tracked file exactly, apart from the header line (the tracked
file still names a `gen_overlays2.py` that no longer exists) and the address
shifts caused by the stale ELF described in step 2.

### 7. Symbol files for the patch build (only when the decomp's symbols change)

    python3 tools/gen_reference_syms.py ~/pokemonsnap/build/pokemonsnap.elf \
        patches/pokemonsnap.syms.toml patches/game_syms.ld

Both outputs are tracked. They contain only names, addresses and sizes: 4,268
functions across 32 code sections, and 14,100 `PROVIDE(name = 0x...)` lines for
data. Use `build/pokemonsnap.elf` (the plain link that matches the ROM); the
relocatable ELF carries the same symbols only if it was linked from the same
tree.

### 8. Build and recompile the game-side patches

    make -C patches DECOMP=$HOME/pokemonsnap
    ~/N64Recomp/build/N64Recomp patches.toml

The first compiles `patches/src/*.c` with the decomp's IDO 7.1 against the
decomp's headers and links `patches/build/patches.elf` with
`mips-linux-gnu-ld` (`patches/Makefile`, `patches/patch.ld`, `patches/game_syms.ld`).
The second writes `RecompiledPatches/patches.c`, which CMake compiles straight
into the executable so that each patched function is resolved before the
linker reaches the recompiled game. `patches/README.md` explains the mechanism.

### 9. The audio microcode (generated by the build, from your ROM)

Nothing to run by hand. `aspMain`, the game's RSP audio microcode (ROM
0x3E580, 0xE20 bytes, loaded at IMEM 0x1080), is recompiled to C++ during the
Windows build: CMake builds RSPRecomp from the vendored
`lib/N64ModernRuntime/N64Recomp`, configures `rsp/aspMain.us.toml.in` into
`build-win/rsp/aspMain.us.toml` with absolute paths, and runs it to write
`build-win/rsp/aspMain.cpp`, which is compiled into the executable. The
translation is derived from the ROM and is never committed (`.gitignore`
refuses `rsp/aspMain.cpp`); `NOTICE.md` says so under its last heading.

The ROM is named by the `SNAP_ROM` cache variable. Left empty, configure
fills it from the first of `pokemonsnap.z64` in the port root (where step 3
put it), `build-win/Release/pokemonsnap.z64` (where the executable runs from)
and `build-win/pokemonsnap.z64` that exists, and stops with an error naming
all three places when none does. It must be a big-endian `.z64` whose header
name is `POKEMON SNAP`; a byte-swapped or little-endian dump is refused at
configure time. Pass `-DSNAP_ROM=<path>` to name another file. The ROM is a
build dependency: changing it, the template or RSPRecomp regenerates the
microcode on the next build.

### 10. Vendored trees on the Windows side

These directories are **not in git** and must exist before CMake runs
(`VENDORING.md` has the pins and what is known about their state):

| Directory | What | How to obtain |
| --- | --- | --- |
| `lib/SDL` | SDL 2.30.11, `release-2.30.11` (`fa24d868ac2f8fd558e4e914c9863411245db8fd`) | `git clone https://github.com/libsdl-org/SDL.git lib/SDL && git -C lib/SDL checkout fa24d868ac2f8fd558e4e914c9863411245db8fd` |
| `lib/DirectX-Headers` | `v1.619.5` (`ee479f0bd5f7b884f202bcf0c3f076cc050dd256`) | `git clone https://github.com/microsoft/DirectX-Headers.git lib/DirectX-Headers && git -C lib/DirectX-Headers checkout ee479f0bd5f7b884f202bcf0c3f076cc050dd256` |
| `lib/rt64/src/contrib` | RT64's 19 third-party trees (396 MB, including the `dxc` compiler binaries) | **No recorded pins.** The only copy is the developer's working tree. See VENDORING.md. |

`lib/N64ModernRuntime` and `lib/rt64/src` (outside `contrib`) are tracked as
plain files and carry local modifications; they are part of the checkout.

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
(`project(Snap64Recomp VERSION 1.0.0)` plus `SNAP_VERSION_PRERELEASE`, `rc1`
today, empty for a final), and reaches the title bar, the log banner, the
title screen's credits line, the executable's version resource (Properties >
Details) and the package name from there.

### 12. Runtime files next to the executable

The build stages these itself (a `POST_BUILD` step of the `Snap64Recomp`
target in `CMakeLists.txt`); the ROM is the one file to copy by hand:

| File | Comes from |
| --- | --- |
| `SDL2.dll` | the `SDL2` target built from `lib/SDL` (step 11) |
| `dxcompiler.dll`, `dxil.dll` | `lib/rt64/src/contrib/dxc/bin/x64/` (the same files RT64's CMake copies into `build-win/lib/rt64/`) |
| `menu_text/recomp_logo.png` | the repository's `menu_text/` (the title-screen badge; absent file, absent badge) |
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

writes `Snap64Recomp-1.0.0-rc1-win64.zip` and a `.sha256` beside it in
`build-win`. The ZIP holds one folder of the same name: `Snap64Recomp.exe`,
`Snap64Recomp.map`, the three DLLs, `menu_text/recomp_logo.png`, `LICENSE`,
`NOTICE.md`, `README.md` and `licenses/` -- one `.txt` per component in
`NOTICE.md`, copied from the vendored trees at packaging time so they cannot
drift from what was built (`concurrentqueue.txt` is cut from that header's
leading comment at configure time). No ROM, no saves, no settings, no cache.

Two licence texts come from the repository's `licenses/` instead, because the
vendored copies carry none:

* `licenses/nlohmann-json.txt` -- tracked: the MIT text with the copyright
  line from `json.hpp`'s SPDX header.
* `licenses/DirectXShaderCompiler.txt` -- tracked: the `dxc` binaries under
  `lib/rt64/src/contrib/dxc/` ship without their `LICENSE.TXT`, so the text
  was taken from upstream's
  <https://raw.githubusercontent.com/microsoft/DirectXShaderCompiler/main/LICENSE.TXT>
  on 2026-09-02 (the LLVM Release License, University of Illinois/NCSA). If
  the file is ever removed, configure warns and `cpack` stops on it: a package
  without the DXC licence is not meant to be produced.

To cut a release: change `project(Snap64Recomp VERSION ...)` and
`SNAP_VERSION_PRERELEASE` in `CMakeLists.txt`, reconfigure, rebuild, `cpack`.
The credits face on the title screen is harvested from the copyright block
and has no hyphen and no `2`, `3` or `7` (`src/version.h.in`); a version
that needs one of those is reported at the first main-menu load
(`[SNAP-MENU] no glyph ...`) and the port's menu strings are withheld until
the string is changed.

## What a clean checkout is missing

A `git clone` of this repository today contains the port's sources, the
tracked copies of N64ModernRuntime and RT64, the recompiler configs, the
tools, the patch sources, the microcode template `rsp/aspMain.us.toml.in` and
the two symbol files. It does **not** contain:

1. `lib/SDL` and `lib/DirectX-Headers` -- ignored; pins are recorded above.
2. `lib/rt64/src/contrib` -- ignored except for `plume/plume_d3d12.cpp`; pins
   were lost with the deleted git directory (VENDORING.md). CMake cannot
   configure without it.
3. `RecompiledFuncs/` and `RecompiledPatches/` -- generated (steps 4-5, 8);
   generating them needs the ROM, the decomp build and N64Recomp under WSL.
4. `pokemonsnap.relocs.elf` and `pokemonsnap.z64` in the port root -- the
   recompiler's inputs (step 3); the ROM is also what CMake recompiles the
   audio microcode from (step 9, `SNAP_ROM`).
5. The decomp, IDO, the MIPS binutils and N64Recomp themselves.
6. `pokemonsnap.z64` in the port root (or `SNAP_ROM`), which configure needs
   for the audio microcode (step 9).

`.gitmodules` used to declare `lib/N64ModernRuntime` and `lib/rt64` as
submodules without ever committing a gitlink; it has been removed, and
VENDORING.md records what those directories actually are.

There is no CI and no test suite. Packaging is `cpack` (step 13): the
`install()` rules lay out the portable folder, and the build stages the DLLs
and the `menu_text` badge beside the executable (step 12). The ROM is the one
file still placed by hand.
