# Vendored dependencies

What `lib/` actually contains, how each piece is tracked, and the upstream
commit each piece is pinned to. `NOTICE.md` lists licenses; `BUILDING.md`
says how to fetch it; `tools/fetch_deps.py` fetches it.

## Submodules, on forks

Every third-party tree under `lib/` except `SlotMap` is a git submodule at
an exact commit. Where the port changes a tree, the submodule is Snap64
Recomp's fork of it, and the changes are commits on the fork's `snap64`
branch on top of the upstream commit; the fork's `main` is upstream's and
carries nothing of the port's.

| Path | Repository, branch | Commit | What it is |
| --- | --- | --- | --- |
| `lib/rt64` | <https://github.com/JackandBeans/rt64>, `snap64` | `9cb7f28` | rt64/rt64 `4337374` plus two commits: the port's changes (74 files, with `.gitmodules` and the plume gitlink pointing at the plume fork), and dxc allowed to differ from its commit (below, "dxc") |
| `lib/rt64/src/contrib/plume` | <https://github.com/JackandBeans/plume>, `snap64` | `da8649e` | plume `d890ac8`, the commit rt64 pins, plus one commit (five files) |
| `lib/rt64/src/contrib/*`, the rest | rt64's own submodules | rt64's pins | the table below |
| `lib/N64ModernRuntime` | <https://github.com/JackandBeans/N64ModernRuntime>, `snap64` | `7ab8900` | N64ModernRuntime `cdf5abb` plus one commit (eighteen files, with `.gitmodules` and the N64Recomp gitlink pointing at the N64Recomp fork) |
| `lib/N64ModernRuntime/N64Recomp` | <https://github.com/JackandBeans/N64Recomp>, `snap64` | `a726e15` | N64Recomp `81213c1`, the commit the runtime pins, plus one commit (`include/recomp.h`) |
| `lib/N64ModernRuntime/thirdparty/*` | the runtime's own submodules (miniz, o1heap, xxHash) | the runtime's pins | upstream, unchanged |
| `lib/SDL` | <https://github.com/libsdl-org/SDL>, upstream | `fa24d86` | `release-2.30.11`, unchanged |
| `lib/DirectX-Headers` | <https://github.com/microsoft/DirectX-Headers>, upstream | `ee479f0` | `v1.619.5`, unchanged |

A fork's changes are the diff from the upstream commit to `snap64`, on
GitHub as the fork's compare page, for example
<https://github.com/JackandBeans/rt64/compare/43373749dac9bbc1b653e6a02aed40a9e1783bed...snap64>.
`python tools/fetch_deps.py` checks all of it out (BUILDING.md step 10).
The port's changes are not offered upstream: rt64, N64ModernRuntime and
plume do not accept contributions made with an LLM (their `CONTRIBUTING.md`),
and every change here was made with Claude Code.

The trees were carried three ways before this. Until 2026-09-02 a
`.gitmodules` declared `lib/N64ModernRuntime` and `lib/rt64` as submodules
with no gitlink ever committed, so a clone could not configure; it was
deleted. From then until 2026-09-27 both were tracked copies edited in
place, their changes written out as `SNAP64-CHANGES.patch` beside each (and
plume's as `lib/rt64/SNAP64-PLUME-CHANGES.patch`, its five files
force-tracked), and RT64's `src/contrib`, SDL and DirectX-Headers were
fetched by `tools/fetch_deps.py` at the pins below. On 2026-09-27 each patch
was applied to its upstream commit on a fork and the result compared blob for
blob with the tracked tree: RT64's 301 files, the runtime's 76 and plume's
five matched with no difference. The comparison found one change no patch
had recorded, N64Recomp's `include/recomp.h` (below, "N64Recomp"), in the
tree since 2026-08-17; it is the fork's one commit.

## Pins

The commits every checkout sits at. The first two rows are the upstream
commits the forks are based on; the rest are what `git submodule status
--recursive` prints (`python tools/fetch_deps.py --list`). The WSL-side tools
are pinned in BUILDING.md (N64Recomp `ffb39cd`, decomp `3a236dc`).

| Path | Upstream | Commit | What it is | Confidence |
| --- | --- | --- | --- | --- |
| `lib/N64ModernRuntime` (fork base) | https://github.com/N64Recomp/N64ModernRuntime.git | `cdf5abbd5026fef5c364c676e4667c45e42b6863` | main, 2026-08-30, "Add CLI options to select games and game modes. (#153)", still upstream's head on 2026-09-27 | exact |
| `lib/N64ModernRuntime/N64Recomp` (fork base) | https://github.com/N64Recomp/N64Recomp | `81213c1831fab2521a6a5459c67b63437d67e253` | the runtime's pin, mod-tool-release-20-g81213c1 | exact |
| `lib/rt64` (fork base) | https://github.com/rt64/rt64.git | `43373749dac9bbc1b653e6a02aed40a9e1783bed` | main, 2026-09-02, "Don't consider VIs with inverted regions as valid. (#264)", still upstream's head on 2026-09-27 | exact (rebased onto it 2026-09-16; the earlier base `a012a23` had been inferred from content) |
| `lib/SDL` | https://github.com/libsdl-org/SDL.git | `fa24d868ac2f8fd558e4e914c9863411245db8fd` | `release-2.30.11` | exact |
| `lib/DirectX-Headers` | https://github.com/microsoft/DirectX-Headers.git | `ee479f0bd5f7b884f202bcf0c3f076cc050dd256` | `v1.619.5` | exact |
| `contrib/ddspp` | https://github.com/redorav/ddspp.git | `21ca0c4319dfd5a161c5f2a0c406e8f60194ea6c` | tag 1.11, 2024-08-07 | exact |
| `contrib/dxc` | https://github.com/rt64/dxc-bin | `cc15e715ee378a4f675b335bd1071ff105873fc8` | 2024-05-16, "Add x64/macos v1.8.2403.2"; the binaries' origins are below. `bin/x64/dxil.dll` is then **replaced** by the file of Microsoft's release v1.7.2308 (below, "dxc") | exact |
| `contrib/hlslpp` | https://github.com/redorav/hlslpp | `6f5274c66132e8f951c400103d897582b8f21491` | tag 3.6, 2024-12-22 | exact |
| `contrib/im3d` | https://github.com/john-chapman/im3d | `d03941725fd0bd08c78c46e3e5b0265526e9d060` | 2023-01-09, "Add Draw Cone. (#60)" | exact |
| `contrib/imgui` | https://github.com/ocornut/imgui | `277ae93c41314ba5f4c7444f37c4319cdf07e8cf` | tag v1.90.4, 2024-02-22 | exact |
| `contrib/implot` | https://github.com/epezent/implot | `f156599faefe316f7dd20fe6c783bf87c8bb6fd9` | v0.16-14-gf156599, 2024-01-22 | exact |
| `contrib/mupen64plus-core` | https://github.com/mupen64plus/mupen64plus-core | `860fac3fbae94194a392c1d9857e185eda6d083e` | 2.5.9-484-g860fac3, 2024-01-24 | exact |
| `contrib/mupen64plus-win32-deps` | https://github.com/mupen64plus/mupen64plus-win32-deps | `de8111fdcb89144abc16c85650ce4e21e028bfb5` | 2.5-21-gde8111f, 2023-03-02 | exact |
| `contrib/nativefiledialog-extended` | https://github.com/btzy/nativefiledialog-extended | `17b6e8ce219c0677f94b63636abb9296b28841ca` | v1.1.1-6-g17b6e8c, 2024-02-24 | exact |
| `contrib/plume` (fork base) | https://github.com/renderbag/plume.git | `d890ac899e505fb30040e037a4037cdeca68f033` | main, 2026-07-22, "manually reset nullbuffer (#105)", the commit rt64 `4337374` pins (until 2026-09-16 the port was on `51b1ad4`, a side branch) | exact |
| `contrib/plume/contrib/D3D12MemoryAllocator` | https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator | `9ef66bc14edd10dee0de3a545b98578363552f66` | v3.0.1 (plume's gitlink) | exact |
| `contrib/plume/contrib/Vulkan-Headers` | https://github.com/KhronosGroup/Vulkan-Headers | `2fa203425eb4af9dfc6b03f97ef72b0b5bcb8350` | v1.4.335 (plume's gitlink) | exact |
| `contrib/plume/contrib/VulkanMemoryAllocator` | https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator | `29b35ea4232688c0f42cdff0c10848290760a417` | v3.2.1-5-g29b35ea (plume's gitlink) | exact |
| `contrib/plume/contrib/volk` | https://github.com/zeux/volk | `be3dbd49bf77052665e96b6c7484af855e7e5f67` | vulkan-sdk-1.4.321.0-7-gbe3dbd4 (plume's gitlink) | exact |
| `contrib/re-spirv` | https://github.com/rt64/re-spirv | `5d6b756ee62760f71b65d37e41a0b5a3dab90507` | 2025-05-03, "Missing cstd include." | exact |
| `contrib/re-spirv/external/SPIRV-Headers` | https://github.com/KhronosGroup/SPIRV-Headers | `f013f08e4455bcc1f0eed8e3dd5e2009682656d9` | vulkan-sdk-1.3.290.0-5-gf013f08 (re-spirv's gitlink) | exact |
| `contrib/spirv-cross` | https://github.com/KhronosGroup/SPIRV-Cross.git | `6173e24b31f09a0c3217103a130e74c4ddec14a6` | vulkan-sdk-1.4.304.0-2-g6173e24b, 2024-12-13 | exact |
| `contrib/stb` | https://github.com/nothings/stb | `ae721c50eaf761660b4f90cc590453cdb0c2acd0` | 2024-02-12 | exact |
| `contrib/xxHash` | https://github.com/Cyan4973/xxHash | `1864a50c9b5cf8500d8e9e61ed92aa0dd3772750` | dev branch, 2024-02-12 (v0.7.4-707-g1864a50) | exact |
| `contrib/zstd` | https://github.com/facebook/zstd | `0ff651dd876823b99fa5c5f53292be28381aee9b` | dev branch, 2024-07-16 (merge of PR #4096) | high: 636 of 638 files match; `tests/cli-tests/bin/unzstd` and `zstdcat` are symlinks upstream and were empty files here (nothing compiled) |
| `contrib/json`, `miniz`, `plainargs`, `project64`, `utf8conv` | https://github.com/rt64/rt64.git | plain files of rt64's own tree | not submodules; they come with the fork's checkout | exact |

`contrib/` above is `lib/rt64/src/contrib/`. rt64's `.gitmodules` also
declares `src/contrib/xess`, for which no gitlink exists upstream either;
nothing in the build refers to it. `metal-cpp`, which the Apple build uses,
is plain files inside plume.

### How the pins were established (2026-09-02, before the forks)

The commits of the contrib submodules lived only in the deleted
`.git/modules/lib/rt64` and could not be read from this repository. They were
recovered from content:

* **The RT64 fork base.** The blob hashes of all 300 tracked `lib/rt64` files
  were compared with `git ls-tree -r` of every commit of rt64/rt64 (all
  branches and pull-request heads, 999 commits). 235 of the 260 files that
  carry no port marker match `a012a23`, the maximum anywhere in the history;
  the remaining 69 files (below) match no upstream blob at any commit, and a
  line diff of each against every upstream version of it shows they are the
  `a012a23` versions plus the port's edits. Three files independently rule out
  any later base: `.github/workflows/validate.yml` is `a012a23`'s and not the
  version the very next commit on main introduced; `CMakeLists.txt` lacks the
  SDL2 fix of `0ca23a4`; the plume checkout is `a012a23`'s pin `51b1ad4`, not
  the later bump to `d890ac8`. Two later upstream changes are present inside
  marked files as hand back-ports (no upstream commit combines them with the
  pre-`3b6b220` files): #259 `f0933d2` (2026-07-23; 15 lines in
  `hle/rt64_state.cpp`, `rt64_framebuffer_manager.cpp/.h`) and #262 `5473732`
  (2026-08-02; 3 lines in `hle/rt64_rsp.cpp`). The tree of `a012a23` is
  byte-identical to `fbc61f3` on branch `tile-sync-fixes`.
* **The contrib submodules** are the 15 gitlinks of `a012a23` (rt64's
  `.gitmodules` is identical to upstream's). Each local directory was hashed
  into a temporary git index (`add -A`, `write-tree`) and compared file by
  file with the pinned commit's tree: 14 of 15 are content-identical (7 also
  match the tree hash exactly; the rest differ only by CRLF normalization,
  whose raw bytes hash to the upstream blob, or by executable bits, which
  NTFS does not keep), plume differs in the port's three files, zstd in the
  two symlink stand-ins. Each directory still carries a `.git` gitfile
  pointing at `.git/modules/lib/rt64/modules/src/contrib/<name>`, confirming
  it was a submodule checkout. The trees nested inside plume and re-spirv were
  compared the same way against the gitlinks in their parents' pinned trees.
* **dxc** is not a release drop but rt64's `dxc-bin` repository at `cc15e71`:
  all 17 files are that commit's blobs. By SHA-256 against Microsoft's release
  assets: `bin/x64/dxil.dll` is the file of v1.7.2212 (`dxc_2022_12_16.zip`);
  `bin/x64/dxc-linux`, `lib/x64/libdxcompiler.so`, `lib/x64/libdxil.so` and
  the four headers in `inc/` are the files of v1.8.2403.2
  (`linux_dxc_2024_03_29.x86_64.tar.gz`); `bin/x64/dxc.exe` and
  `bin/x64/dxcompiler.dll` (FileVersion 1.7.0.4147, ProductVersion
  `1.7.0.4147 (0dc8d9060)`) are a private build of DXC main commit
  `0dc8d9060` of 2023-10-09 that matches no release (they entered `dxc-bin`
  in its first commit, with `lib/x64/dxcompiler.lib`); the arm64 Linux
  binaries are private builds and the macOS ones are LunarG Vulkan SDK
  builds. `tools/fetch_deps.py` records every file's SHA-256 and origin and
  checks them after each fetch.

  **`bin/x64/dxil.dll` is not shipped as dxc-bin has it.** The release
  README of every DXC archive assigns `dxil.dll` to `LICENSE-MS.txt`,
  Microsoft Software License Terms, and the v1.7.2212 text (the release
  dxc-bin's file is from) is a time-limited pre-release agreement: it
  terminates thirty days after a commercial release, allows use "solely on
  Windows", and prohibits sharing, publishing or distributing the software,
  with no distributable-code clause. The port ships `dxil.dll` beside the
  executable, so it takes the file of the next release, **v1.7.2308**
  (`dxc_2023_08_14.zip`, SHA-256 `01d4c4df…`; `bin/x64/dxil.dll` SHA-256
  `9cccc7ef…`, FileVersion 101.7.2308.12), whose `LICENSE-MS.txt` carries a
  "Distributable Code" section; the text is the same in every later release
  up to v1.9.2607 (checked 2026-09-02) and is tracked as
  `licenses/DirectXShaderCompiler-dxil.txt` and shipped (NOTICE.md). Not a
  newer validator: the compiler in dxc-bin is a 1.7-series build, and on
  2026-09-02 the v1.8.2403.2 validator refused its library shaders at build
  time (`RasterPSLibrary.hlsl`: "Container part 'Runtime Data (RDAT)' does
  not match expected for module"), whereas the 1.7.2308 validator signs
  everything the 1.7 compiler emits. `tools/fetch_deps.py` downloads the
  archive, checks it, takes the one file out of it and records the SHA-256.
  The build was repeated with this file, the 53 shaders recompiled and signed
  through it, and the game run (BUILDING.md, "The clean-checkout build").
* **SDL and DirectX-Headers** were read from their live `.git` directories
  (`rev-parse HEAD`, `describe --tags --exact-match`), confirmed against the
  GitHub tag refs, and compared file by file with `ls-tree -r HEAD`: no
  content differences.

Proof: on 2026-09-02 a plain `git clone` of this repository into a second
directory, `python tools/fetch_deps.py`, the three generated inputs copied in,
and CMake with the Visual Studio 16 2019 x64 generator produced
`Snap64Recomp.exe` with the three DLLs beside it; the fetched trees were then
diffed against mine and matched apart from the two zstd symlink
stand-ins (BUILDING.md, "What a clean checkout is missing", has the record).

Proof of the conversion to submodules (2026-09-27): the fetched checkouts
were compared byte for byte with the trees the 1.1.0 build had used. Of
RT64's 10,262 files 9,034 were identical, 1,225 differed only in line
endings, and three in content: the `.gitmodules` that points plume at the
fork, and the two zstd symlinks, which git now writes as the symlinks'
targets (nothing compiles them). The runtime's 1,197: 857 identical, 339 in
line endings only, and its `.gitmodules`. The line endings are RT64's and the
runtime's own sources, which the old tracked copies had in CRLF: every
submodule is now checked out exactly as its repository stores it, on every
machine (`tools/fetch_deps.py` sets `core.autocrlf=false` in each), as GitHub's
Linux and Mac runners always had them. Those sources hold no raw string
literal, so no line ending reaches the binary through them. The Windows
build from the new checkouts recompiled everything: its 109 shader blobs are
byte-identical to 1.1.0's, and its executable differs from 1.1.0's (SHA-256
`3006d9b4...`) in four bytes, the two link timestamps.

## Local modifications to vendored trees

Both trees are **forks, not pristine**: the commits on each fork's `snap64`
branch. Anything that moves them to a newer upstream must keep the following
(below, "Changing RT64 or the runtime", says how). The string `Pokemon Snap
port` marks most changed sites (grep for it), but not all of them.

### N64ModernRuntime

The fork's `snap64` is upstream commit `cdf5abb` (2026-08-30) with the port's
changes in one commit, eighteen files. Every changed file carries a `Pokemon
Snap port` marker. The 2026-09-16 update to `cdf5abb` was made from a copy
whose base had never been recorded (it matched the tree of `03c3bd8`,
2026-05-17; three files conflicted).

* `ultramodern/src/threads.cpp` -- pooled host threads, the replenisher, the
  per-guest-thread run clock, and the thread registry the stall report reads;
  and a thread that ends with code made at run time on its stack (a mod's
  code, or a function recompiled live for a mod's hook) returns to its entry
  without unwinding, since that code has no unwind information and the
  exception that ends a thread cannot pass it. The game parks every process
  in `ohWait` and ends it from outside, so a hook on any function that waits
  stopped the game when its process ended. Without code mods every thread
  ends by the exception as before. And the host threads that run game code
  (the pool's workers, and the game's first thread in
  `librecomp/src/recomp.cpp`) get an 8 MB stack on macOS
  (`make_game_host_thread`, `start_detached_game_host_thread`, declared in
  `ultramodern/include/ultramodern/threads.hpp`): Windows gives every thread
  the executable's `/STACK` (8 MB) and Linux 8 MB, but macOS gives all but
  the main thread 512 KB. Elsewhere they are the plain `std::thread`s they
  were.
* `ultramodern/src/mesgqueue.cpp` -- run-clock pauses.
* `ultramodern/src/timer.cpp` -- the speed ratio behind fast forward and slow
  motion.
* `ultramodern/src/events.cpp` -- the VI tick before the game has chosen a
  mode does nothing (found on a macOS build, pull request #2; upstream has the
  same guard now).
* `ultramodern/src/input.cpp` -- the linear stick mapping (127 times the
  axis) the port's mouse, gyro and keyboard values are scaled for. Upstream
  gates every stick through an N64-shaped octagon since August 2026; with it,
  the scoring replay reached 21 photos instead of 45. A physical stick's gate
  is the port's to add in its own layer.
* `ultramodern/include/ultramodern/ultramodern.hpp`, `librecomp/src/pi.cpp`,
  `librecomp/src/rsp.cpp` -- the matching interface and I/O changes.
* `ultramodern/include/ultramodern/input.hpp` -- `Pak::ControllerPak`
  uncommented, so a port can report a pak that the rumble path does not
  claim (the Snap Station on port 4, `src/snap_station.cpp`).
* `librecomp/src/recomp.cpp` -- the ROM check names the exact fault (missing,
  unreadable, not a ROM, the wrong dump with both hashes) and never rewrites
  or removes the player's file; byte-swapped dumps are corrected in memory.
  And a game thread that starts after `quit()` has cleared the current game
  starts nothing, where `current_game.value()` threw on the way out (found
  by DramaticShape's VR fork).
* `librecomp/src/files.cpp`, `librecomp/include/librecomp/files.hpp` -- the
  data directory and file handling the port's paths need.
* `librecomp/CMakeLists.txt`, `ultramodern/CMakeLists.txt` -- build options.
* `librecomp/src/mods.cpp` -- a mod whose content cannot change while it
  is loaded (a code mod, loaded before the game runs) takes an enable or
  disable for the next start instead of refusing it: the port's Mods page
  is inside the game, where upstream's launcher toggles mods before the
  game starts. It also registers each block of live-recompiled code with
  ultramodern (above), and says in the log which function a failed live
  recompilation stopped at, where upstream reports only "Code mod loading
  internal error". `librecomp/include/librecomp/mods.hpp` declares the
  code handle's destructor for the first.
* `librecomp/src/mod_manifest.cpp` -- a mod option's `hidden_from` and
  `disabled_from` rules reach the mod's config. Upstream parses and checks
  them, then adds them to a copy of the config's schema that is thrown
  away, so no rule a manifest declared ever took effect; the port adds them
  through the config's own `add_option_hidden_dependency` and
  `add_option_disable_dependency`. Upstream's `main` has the same code
  (checked 2026-09-26, the file last changed there in `589bbf0`).

### N64Recomp

The runtime's copy of the recompiler (its headers are what the recompiled
game includes; its `RSPRecomp` builds the audio microcode) is N64Recomp
`81213c1` with one change, the fork's one commit:

* `include/recomp.h` -- under MSVC, `RELOC_HI16` and `RELOC_LO16` compute
  their sum through a volatile. MSVC folds `(int16_t)LO16(sum)` into an add
  that is never truncated, so an address whose low half carries into the
  high half lands 64 KB off; the volatile keeps the 16-bit wrap the MIPS
  `lui`/`addiu` pair depends on. Clang, which the other recompilations use
  on Windows, does not fold it. In the tree since the first commit
  (2026-08-17) and recorded by no patch until 2026-09-27.

### The runtime, continued

Two things upstream changed after the old base are answered on the port's
side rather than in the runtime: the runtime no longer switches present-early
on for the port at the first task, so `src/rt64_render_context.cpp` does it
there itself; and `recomp::GameEntry` requires a display name, which
`src/main.cpp` sets.

### RT64

The fork's `snap64` is rt64/rt64 `4337374` (2026-09-02) with the port's
changes in one commit (74 files outside `src/contrib`, with `.gitmodules`
and the plume gitlink pointing at the plume fork) and a second that lets dxc's checkout differ from
its commit (the `dxil.dll` below). The 2026-09-16 rebase
from `a012a23` (eleven upstream commits: the RDNA4 Vulkan workaround, VIs
with inverted regions, the viewport clip rect in draw-area detection, tile
synchronization, and the plume bump that fixes Metal leaks) applied with no
textual conflict. One upstream change is taken differently: RT64 now forces
Vulkan on RDNA4 cards with a driver up to `0x200000794103EC` whatever API the
player chose; this port's suite and a full playthrough ran on an RX 9060 XT
at exactly that driver in D3D12, and the API is the player's choice, so in `hle/rt64_application.cpp` that clause
applies only in Automatic mode, which the port never uses. The lists below
describe the changes by area as they were cataloged against `a012a23`;
`shaders/TextureDecodeCS.hlsl` joined them on 2026-09-10.

**Forty-eight files carry the marker**: forty-seven under `lib/rt64/src` and
`lib/rt64/include/rt64_extended_gbi.h`. Forty-three are modified upstream
files and five are new (`hle/rt64_snap_diag.h`, `hle/rt64_snap_overlay.h`,
`hle/rt64_snap_photo_detail.h`, `render/rt64_shader_blob_cache.h`,
`render/rt64_snap_recolor.h`). By area:
object identity and transform-group pairing (`hle/rt64_state.cpp/.h`,
`rt64_rigid_body.cpp/.h`, `rt64_transform_group.h`, `rt64_draw_call.h`,
`rt64_projection.h`, `rt64_rdp.cpp/.h`, `rt64_rsp.cpp`,
`gbi/rt64_gbi.cpp`, `gbi/rt64_gbi_extended.cpp`, `gbi/rt64_gbi_rdp.cpp`,
`include/rt64_extended_gbi.h`), frame pacing and presentation
(`hle/rt64_present_queue.cpp/.h`, `rt64_workload_queue.cpp/.h`,
`rt64_workload.h`, `rt64_game_frame.cpp/.h`, `rt64_vi.cpp/.h`,
`rt64_application.cpp`, `render/rt64_vi_renderer.cpp/.h`,
`render/rt64_framebuffer_renderer.cpp/.h`, `render/rt64_projection_processor.cpp`),
framebuffer readback and photo detail (`hle/rt64_framebuffer.h`,
`rt64_framebuffer_manager.cpp/.h`, `rt64_snap_photo_detail.h`,
`shaders/TextureCopyPS.hlsl`, `shared/rt64_texture_copy.h`), the Vulkan
image format the texture decoder declares (`shaders/TextureDecodeCS.hlsl`),
the shader seen-list warmer (`render/rt64_raster_shader_cache.cpp`,
`render/rt64_shader_blob_cache.h`), the Jynx recolor
(`render/rt64_snap_recolor.h`), the depth of primitive-depth sprites and the
per-call parameters the pixel stage reads (`shaders/RasterPS.hlsl`,
`shaders/RasterVS.hlsl`, `shared/rt64_rdp_params.h`,
`shared/rt64_framebuffer_params.h`), the Snap Station's overlay
(`hle/rt64_snap_overlay.h`), the port's diagnostics header
(`hle/rt64_snap_diag.h`), and configuration fields
(`common/rt64_user_configuration.h`, `rt64_enhancement_configuration.h`). The macOS build adds one line to `common/rt64_hlslpp.h`, the C standard
library included before hlsl++. One fix is upstream's bug rather than the
port's need: `hle/rt64_present_queue.cpp` releases the swap chain's
framebuffers before it resizes the swap chain, where upstream resizes first
and Direct3D 12 refuses while the old images are still referenced (found by
DramaticShape's VR fork, 2026-09-25).

**Twenty-three more files differ without the marker**; a grep for the marker
does not find them. Twenty-two are modified upstream files: `CMakeLists.txt`
(one added line), `src/common/rt64_enhancement_configuration.cpp`,
`rt64_replacement_database.h`, `rt64_user_configuration.cpp`,
`rt64_user_paths.cpp/.h`,
`src/hle/rt64_application.h`, `rt64_application_window.cpp`,
`rt64_framebuffer_pair.cpp`, `rt64_projection.cpp`, `rt64_rsp.h`,
`rt64_workload.cpp`, `src/render/rt64_raster_shader.cpp/.h`,
`rt64_raster_shader_cache.h`, `rt64_render_target.cpp`,
`rt64_transform_processor.cpp`, `src/shaders/Depth.hlsli`, `Formats.hlsli`,
`TextureSampler.hlsli`
(a mip level clamp; the previous version of this file called it suspect, and
it is modified), `src/shared/rt64_other_mode.h` and
`src/tools/texture_hasher/texture_hasher.cpp`; and
`src/render/rt64_shader_blob_cache.cpp` is new (409 lines, no upstream
counterpart). The marked files also contain the two hand back-ports named
above (#259, #262).

### plume

Five files differ from plume `d890ac8` (the commit rt64 `4337374` pins;
until 2026-09-16 the base was `51b1ad4`, on a side branch), and none of their
contents exists anywhere in plume's history. They are the one commit on the
fork's `snap64` (until 2026-09-27 they were force-tracked in this
repository, the directory around them ignored):

* `plume_d3d12.cpp` (tracked since commit `7d704d4`, which was `35bcba0`
  before the history rewrite of 2026-09-02). It carries four changes: the
  null guard in `setSamplePositions` described below; `D3D12SwapChain::present`
  keeping a sync interval of 1 when vsync is on even with present wait (the
  comment in the file says why); a Direct3D 12 pipeline-library
  persistence layer (`hashGraphicsPipelineDesc`,
  `D3D12Device::setPipelineCacheData` / `getPipelineCacheData`, the
  `snap_pipeline_reused` / `snap_pipeline_built` counters) that
  `lib/rt64/src/hle/rt64_application.cpp` drives; and, since 2026-09-10,
  `D3D12QueryPool::queryResults` skipping the copy when `Map` returns null
  (a removed device) and printing `[SNAP-D3D12]` with the removal reason
  once, where 1.0.3 crashed on the null.
* `plume_d3d12.h` (tracked since 2026-09-02): ten added lines, the
  `pipelineLibrary`, `pipelineLibraryBlob` and `pipelineLibraryMutex` members
  of `D3D12Device` and the two overrides.
* `plume_render_interface.h` (tracked since 2026-09-02): six added lines, the
  virtual `setPipelineCacheData` / `getPipelineCacheData` on `RenderDevice`
  with default bodies that report no support.
* `plume_vulkan.cpp` (tracked since 2026-09-06): one added branch in
  `VulkanCommandList::copyTextureRegion`, the texture-to-buffer copy
  (`vkCmdCopyImageToBuffer`) upstream lacks; without it a buffer
  destination fell through to the image-to-image path and dereferenced a
  null texture, which took the port's presented-frame capture and the Snap
  Station's sheet capture down on Linux.
* `plume_metal.cpp` (tracked since 2026-09-26): the same branch in
  `MetalCommandList::copyTextureRegion` (the blit encoder's texture-to-buffer
  `copyFromTexture`), for the same two captures on a Mac, where the image-to-
  image path would have dereferenced the same null.

The `.cpp` does not compile against pristine plume headers, so the headers
are in the same commit (until 2026-09-02 they were not tracked at all, and
the only copy of their changes was my working tree).

The file that used to document the null guard
(`lib/rt64/src/contrib/PLUME_PATCHES.md`, in the old ignored directory) is
gone; its substance:

`copyTextureRegion` calls `setSamplePositions(dstLocation.texture)`, and a
texture-to-buffer copy (any readback through a `PlacedFootprint` destination)
has `texture == nullptr` there. Upstream guards this with an `assert`, which is
compiled out in release builds, so the first readback dereferenced null inside
the driver layer -- a reproducible crash at startup for the port's frame-capture
diagnostics. The fix, at the top of `D3D12CommandList::setSamplePositions`:

```cpp
if (texture == nullptr) {
    resetSamplePositions();
    return;
}
```

The `resetSamplePositions()` matters: a non-MSAA destination texture would have
reached the else branch and reset any custom programmable sample positions
before the copy; a buffer destination needs the same reset. Updating plume
means rebasing the fork's commit onto the new one (below); the bug is
upstream's, and stays fixed only in the fork.

## Changing RT64 or the runtime

`tools/fetch_deps.py` leaves each submodule detached at its recorded commit,
one commit deep, with the fork's `snap64` branch ref fetched beside it (a
one-commit fetch brings the commit and no branch name; the script fetches
the name, and says so if the recorded commit is not that branch's head). To
change one of the forked trees:

1. In it (`lib/rt64`, `lib/N64ModernRuntime`, or a fork nested in them,
   `lib/rt64/src/contrib/plume` or `lib/N64ModernRuntime/N64Recomp`):
   `git switch snap64`, and `git fetch --unshallow` if the history is wanted.
2. Change, build, and commit there, by this repository's commit rules.
3. `git push origin snap64`. Only that branch is ever pushed to a fork.
4. Record the new commit in the parent: `git add lib/rt64` here (for a
   nested fork, `git add src/contrib/plume` in `lib/rt64` first, commit and
   push that, then record `lib/rt64` here), and commit.

A pushed parent that points at an unpushed commit leaves every fresh clone
unable to check it out, so the fork is always pushed first. To move a fork to
a newer upstream: `git remote add upstream <upstream URL>`, `git fetch
upstream`, `git rebase <new commit> snap64`, resolve, build, run the release
suite, `git push --force-with-lease origin snap64`, and record it here as in
step 4. The Linux and Mac builds read the same forks, so their workflows
build the new commits as they are.

## `assets/gamecontrollerdb.txt`

The community's pad mappings, <https://github.com/mdqinc/SDL_GameControllerDB>
(zlib; `licenses/SDL_GameControllerDB.txt`), at commit `28a856f2b92d` of
2026-09-07, with a section appended at the end -- marked `Snap64 Recomp` --
holding lines the list lacks (NOTICE.md). Shipped beside the executable and
read by SDL at start-up (`src/input.cpp`, `load_controller_mappings`). To
refresh: fetch the raw file from that repository's `master`, keep the port's
section from the marker down, and update the commit in NOTICE.md and here.

## Generated and derived files

* `patches/game_syms.ld` and `patches/pokemonsnap.syms.toml` are tracked as of
  2026-09-02. They are derived from the decomp's ELF by
  `tools/gen_reference_syms.py` and contain only symbol names, addresses and
  sizes; without them the patch build cannot run from a checkout.
* `pokemonsnap.relocs.elf` and `pokemonsnap.z64` in the port root are the
  recompiler's inputs and are ignored (`*.elf`, `*.z64`); the configs resolve
  them relative to the port root (BUILDING.md step 3).
* `RecompiledFuncs/`, `RecompiledPatches/` and `patches/build/` are generated
  and ignored.
* `dump.toml` and `data_dump.toml` (3.5 MB together) are N64Recomp's dump of
  the ELF's relocations and symbols from 2026-08-14. They are tracked, nothing
  in the build reads them, and they are candidates for removal.
