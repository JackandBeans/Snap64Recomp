# Third-party notices

Snap64 Recomp is distributed under the GNU General Public License, version 3
(`LICENSE`). That is not a choice of style: the executable statically links
`librecomp` and `ultramodern` from N64ModernRuntime, which are GPLv3, so the
combined work can only be distributed under GPLv3. Everything else in the tree
is under a license the GPL can absorb (MIT, BSD, zlib, Apache-2.0, public
domain); the two GPLv2-only items in the tree are not compiled into the
executable (see `mupen64plus-core` and `xxHash/cli` below).

What follows is every component in the working tree as read on 2026-09-02,
with its license as found in its own files, its origin, and whether this build
uses it. "Ignored" means the directory is present on the developer's machine
but not in git (`VENDORING.md` explains).

## Recompilation toolchain and runtime

| Component | License (as found) | Origin | In this tree |
| --- | --- | --- | --- |
| **N64Recomp** | MIT, "Copyright (c) 2024 Wiseguy" (`LICENSE`) | <https://github.com/N64Recomp/N64Recomp> | The tool itself runs under WSL (`~/N64Recomp`, upstream commit `ffb39cd`, unmodified) and is not in this tree. A copy of its sources and headers is bundled inside `lib/N64ModernRuntime/N64Recomp` (tracked; `recomp.h` is what the generated code includes); its upstream commit is not recorded. |
| **N64ModernRuntime** (`librecomp`, `ultramodern`) | GPLv3 (`lib/N64ModernRuntime/COPYING`, byte-identical to gnu.org's `gpl-3.0.txt`) | <https://github.com/N64Recomp/N64ModernRuntime> | Tracked as 1041 plain files. **Modified** by this port: `ultramodern/src/threads.cpp`, `ultramodern/src/mesgqueue.cpp`, `ultramodern/include/ultramodern/ultramodern.hpp`, `librecomp/src/pi.cpp`, `librecomp/src/rsp.cpp` (each site marked `Pokemon Snap port`). Upstream commit not recorded. Statically linked. |
| concurrentqueue (in `N64ModernRuntime/thirdparty`) | Simplified BSD, also Boost Software License (stated in the header) | Cameron Desrochers | header-only, used by both libraries |
| nlohmann/json 3.12.0 (in `N64ModernRuntime/thirdparty/json`) | MIT (SPDX header) | <https://github.com/nlohmann/json> | header-only, used by librecomp |
| miniz (in `N64ModernRuntime/thirdparty/miniz`) | MIT (`LICENSE`: RAD Game Tools, Valve, Rich Geldreich) | <https://github.com/richgel999/miniz> | compiled by librecomp's CMake |
| o1heap (in `N64ModernRuntime/thirdparty/o1heap`) | MIT (Pavel Kirienko) | <https://github.com/pavel-kirienko/o1heap> | compiled into librecomp |
| sse2neon (in `N64ModernRuntime/thirdparty/sse2neon`) | MIT (header) | <https://github.com/DLTcollab/sse2neon> | header-only; ARM builds only |
| xxHash (in `N64ModernRuntime/thirdparty/xxHash`) | BSD 2-Clause (`LICENSE`); `cli/COPYING` is GPLv2 and covers only the command-line tool, which is not used | Yann Collet | header-only (ROM hashing) |

## Renderer

| Component | License (as found) | Origin | In this tree |
| --- | --- | --- | --- |
| **RT64** | MIT, "Copyright (c) 2024 RT64 Contributors" (`lib/rt64/LICENSE`) | <https://github.com/rt64/rt64> | `lib/rt64/src` and `include` tracked (299 files); **modified** by this port in thirty-two files marked `Pokemon Snap port` (thirty-one under `src`, one under `include`) (object identity and transform pairing, frame pacing and presentation, diagnostics, configuration; list in `VENDORING.md`). Upstream commit not recorded. Statically linked. |

RT64's own third-party trees live in `lib/rt64/src/contrib`, which is ignored
by git (except `plume/plume_d3d12.cpp`) and whose upstream pins are lost. Each
entry, with what its files say:

| `contrib/` entry | License (as found) | Used by this build? |
| --- | --- | --- |
| `ddspp` | MIT (Emilio López, 2018-2023) | yes, header-only (DDS texture parsing) |
| `dxc` | **No license file in the tree.** These are DirectX Shader Compiler release binaries (`bin/x64/dxc.exe`, `dxcompiler.dll`, `dxil.dll`, plus Linux and macOS builds, `inc/`, `lib/`); the headers cite "LICENSE.TXT", which is not present. Upstream's `LICENSE.TXT` is the LLVM Release License (University of Illinois/NCSA), read on 2026-09-02 at <https://github.com/microsoft/DirectXShaderCompiler/blob/main/LICENSE.TXT>. The release version is not recorded anywhere in the tree. `dxil.dll` is Microsoft's DXIL validator, shipped with DXC releases; its redistribution terms must be checked against the release these came from before the DLLs are distributed. | yes: `dxc.exe` compiles the shaders at build time; `dxcompiler.dll` and `dxil.dll` are loaded at run time and must sit beside the executable |
| `hlslpp` | MIT (Emilio López, 2017-2024) | yes, header-only (49 RT64 files) |
| `im3d` | MIT-form permission notice (John Chapman, 2016-2022) | yes, `im3d.cpp` is compiled |
| `imgui` | MIT (Omar Cornut, 2014-2024) | yes, compiled with the DX12 and Win32 backends |
| `implot` | MIT (Evan Pezent, 2020) | yes, compiled |
| `json` | nlohmann/json 3.12.0, MIT (second copy of the same library) | yes, header-only |
| `miniz` | single-file miniz (Rich Geldreich, 2013) whose header refers to its "unlicense" (public domain) statement | yes |
| `mupen64plus-core` | **GPLv2** (`LICENSES`) | **no**: only `src/api` is on RT64's include path, and no file outside `contrib` includes anything from it (checked by grep). Nothing from it is compiled or linked. |
| `mupen64plus-win32-deps` | a bundle: SDL2 2.26.3 and SDL2_net 2.2.0 (zlib license), boost 1.81.0, freetype 2.13.0, libpng 1.6.39, zlib 1.2.13, nasm, gawk, OpenGL headers | only `SDL2-2.26.3/include` is referenced by RT64's CMake on Windows; the SDL2 that is linked and shipped is the one built from `lib/SDL`. The rest of the bundle is unused. |
| `nativefiledialog-extended` | zlib (`LICENSE`) | yes, compiled and linked (`nfd`) |
| `plainargs` | public domain (Unlicense text in the header) | header-only |
| `plume` | MIT (renderbag and contributors, 2024) | yes, compiled and linked; `plume_d3d12.cpp` carries this port's readback fix (force-tracked; `VENDORING.md`) |
| `project64` | **no license text in the tree** (two headers, `Base.h` and `Video.h`) | **no**: nothing outside `contrib` references them (checked by grep); not compiled |
| `re-spirv` | MIT (renderbag and contributors, 2024) | yes, compiled and linked |
| `spirv-cross` | Apache-2.0 (`LICENSE`), with Khronos free-use terms for some files | **no on Windows**: only RT64's macOS `spirv_cross_msl` tool builds it |
| `stb` | dual MIT / public domain (`LICENSE`) | yes, header-only; `stb_image.h` is also used by this port's `src/menu_assets.cpp` to load the title badge |
| `utf8conv` | **no license statement in the files** (`utf8conv.h`, `utf8except.h`, "Copyright (C) by Giovanni Dicanio") | included by four RT64 files; terms unverified |
| `xxHash` | BSD 2-Clause (`LICENSE`); `cli/COPYING` GPLv2 covers only the unused command-line tool | yes, header-only |
| `zstd` | dual BSD 3-Clause (`LICENSE`) / GPLv2 (`COPYING`); the BSD terms apply here | yes, `libzstd_static` is compiled and linked |
| `metal-cpp` | referenced by RT64's CMake for Apple builds | not present in this tree |

## Platform libraries

| Component | License (as found) | Origin | In this tree |
| --- | --- | --- | --- |
| **SDL2** 2.30.11 | zlib (`lib/SDL/LICENSE.txt`, Sam Lantinga 1997-2025) | <https://github.com/libsdl-org/SDL>, tag `release-2.30.11`, commit `fa24d868ac2f8fd558e4e914c9863411245db8fd` | `lib/SDL`, ignored, pristine clone; built as `SDL2.dll`, shipped beside the executable |
| **DirectX-Headers** v1.619.5 | MIT (Microsoft) | <https://github.com/microsoft/DirectX-Headers>, commit `ee479f0bd5f7b884f202bcf0c3f076cc050dd256` | `lib/DirectX-Headers`, ignored, pristine clone; headers only |
| Direct3D 12, DXGI, Vulkan loader | system components | Windows / the GPU driver | `d3d12.dll`, `dxgi.dll` and `vulkan-1.dll` are loaded from the system; not vendored |

## Build tooling that is not in this tree

| Tool | License | Where it comes from |
| --- | --- | --- |
| The decompilation | no license file in its repository | <https://github.com/ethteck/pokemonsnap>, checked out at `~/pokemonsnap` under WSL (commit `3a236dc`). The port reads its ELF (symbols and relocations) and compiles the patches against its headers. |
| IDO 7.1 and 5.3 | SGI's proprietary MIPSpro compilers, run as static recompilations | downloaded by the decomp's `configure.py --setup` from <https://github.com/decompals/ido-static-recomp/releases> (v1.1). Used to compile `patches/src`. |
| `mips-linux-gnu-ld`, `mips-linux-gnu-as`, `readelf` | GNU binutils (GPLv3, as tools) | Ubuntu package `binutils-mips-linux-gnu` / `binutils` |
| Visual Studio 2019 (MSVC 19.29), CMake, Python 3, ninja, uv | their own terms | see `BUILDING.md` |

## Files in this tree that derive from the game

These contain material extracted or translated from the Pokémon Snap ROM. They
are here because the port needs them and the generation step has not yet been
moved to build or run time; they are **scheduled for removal** in favour of
generating them from the player's own ROM, and they have **not** been removed
yet.

| File | What it is | How it was made |
| --- | --- | --- |
| `rsp/aspMain.cpp` (73 KB) | a C++ translation, by RSPRecomp, of Nintendo's `aspMain` RSP audio microcode (ROM 0x3E580, 0xE20 bytes of RSP instructions, loaded at IMEM 0x1080) | `~/N64Recomp/build/RSPRecomp aspMain.us.toml` |
| `src/menu_font.h` (72 KB) | the letterforms of the Options screen, cut from the game's pre-rendered menu sprites, as IA pixel pairs, plus thirteen characters drawn by the port in the same style | `tools/harvest_menu_font.py <rom>` (decompresses the main menu's VPK0 segment at ROM 0xA0F830) |
| `src/menu_dot.h` | the Options menu's bullet dot and value chevrons, as pixels | `tools/extract_menu_dot.py <rom>` |
| `tools/menu_font_harvest.json` (51 KB) | the harvested glyph pixels in the harvester's intermediate form | `tools/harvest_menu_font.py` |
| `tools/menu_glyphs.py` | the same dot and chevron pixels as `src/menu_dot.h`, for the reference renderer | `tools/extract_menu_dot.py` |

Also derived from the game, and tracked on purpose:

* `patches/game_syms.ld` and `patches/pokemonsnap.syms.toml` -- symbol names,
  addresses and sizes from the decompilation's ELF; no code or data bytes.
* `patches/src/*.c` -- replacement functions for the game. Where a patch
  reproduces a game function with an addition (`render_patch.c` reproduces
  `renRenderModelTypeACommon` from the decomp's `src/sys/render.c`, per
  `patches/README.md`), the function's origin is the game.
* `pokemonsnap.us.toml` and `src/recomp_overlays.inl` -- addresses, section
  names and sizes of the game's layout.

The ROM itself is never distributed. The generated `RecompiledFuncs/` (the
recompiled game) and `RecompiledPatches/` are ignored by git.

`menu_text/recomp_logo.png` is the port's own title-screen badge, not game art.
