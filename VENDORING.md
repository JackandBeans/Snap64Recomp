# Vendored dependencies

What `lib/` actually contains, how each piece is tracked, and the upstream
commit each piece is pinned to. `NOTICE.md` lists licenses; `BUILDING.md`
lists what to fetch; `tools/fetch_deps.py` fetches it.

## `.gitmodules` is gone, on purpose

Until 2026-09-02 a `.gitmodules` declared `lib/N64ModernRuntime` and `lib/rt64`
as submodules of <https://github.com/N64Recomp/N64ModernRuntime.git> and
<https://github.com/rt64/rt64.git>. No gitlink was ever committed for either
(`git ls-tree -r HEAD | grep ^160000` is empty in every commit), so
`git submodule update --init --recursive` was a no-op and a fresh clone could
not configure. Both directories are ordinary tracked files that have been
edited in place, so the declaration was fiction and has been deleted. If they
are ever turned back into submodules, the local modifications below have to be
carried as patches on top of the upstream commit: RT64's is known (below),
N64ModernRuntime's is not.

## How each tree is tracked

| Path | In git? | State |
| --- | --- | --- |
| `lib/N64ModernRuntime` | yes, 1041 plain files | forked; no git directory; upstream commit unrecorded |
| `lib/N64ModernRuntime/N64Recomp` | yes (part of the above) | the runtime's bundled copy of N64Recomp's headers and sources; its `RSPRecomp` target is built by the port's CMake to recompile the audio microcode at build time (BUILDING.md step 9); upstream commit unrecorded |
| `lib/rt64` (outside `src/contrib`) | yes, 300 files | forked from rt64/rt64 `a012a23` (established by content, below); `lib/rt64/.git` is an orphaned gitfile pointing at a deleted `.git/modules/lib/rt64` |
| `lib/rt64/src/contrib/` | **no** (ignored), except the port's three plume files (below) | RT64's third-party trees, 396 MB; fetched by `tools/fetch_deps.py` at the pins below |
| `lib/SDL` | no (ignored) | fetched by `tools/fetch_deps.py` at the pin below (the developer's copy is a full clone with a live `.git`) |
| `lib/DirectX-Headers` | no (ignored) | fetched by `tools/fetch_deps.py` at the pin below (same) |

## Recovered pins

`python tools/fetch_deps.py` fetches every row of this table except the first
into a detached checkout of exactly that commit, verifies it, and is a no-op
when the tree is already there (BUILDING.md step 10). The WSL-side tools are
pinned in BUILDING.md (N64Recomp `ffb39cd`, decomp `3a236dc`).

| Path | Upstream | Commit | What it is | Confidence |
| --- | --- | --- | --- | --- |
| `lib/rt64` (fork base; not fetched, tracked) | https://github.com/rt64/rt64.git | `a012a2301908b130f9251dd3ec0aaeebf9678d80` | main, 2026-07-22, "Improve synchronization detection for tiles being sampled. (#254)" | high (inferred from content; no gitlink was ever recorded) |
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
| `contrib/plume` | https://github.com/renderbag/plume.git | `51b1ad443b9f202c5cfc930ae25345d3f2ba7716` | 2026-01-28, "Force residency sets to off."; on branch `metal-release-pool-refactor-plus-sets-off`, not on `main` | high: 863 of 866 files match; the other three are the port's own (below) |
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
| `contrib/json`, `miniz`, `plainargs`, `project64`, `utf8conv` | https://github.com/rt64/rt64.git | plain files of rt64's own tree at `a012a23` (8 files) | not submodules; copied from that commit and verified by blob hash | exact |

`contrib/` above is `lib/rt64/src/contrib/`. rt64's `.gitmodules` also
declares `src/contrib/xess`, for which no gitlink exists upstream either;
nothing in the build refers to it, and `metal-cpp` (referenced by RT64's CMake
for Apple builds) is not a submodule and not present.

### How the pins were established (2026-09-02)

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
  match the tree hash exactly; the rest differ only by CRLF normalisation,
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
diffed against the developer's and matched apart from the two zstd symlink
stand-ins (BUILDING.md, "What a clean checkout is missing", has the record).

## Local modifications to vendored trees

Both tracked trees are **forked, not pristine**. Anything that re-syncs them
from upstream must preserve the following. The string `Pokemon Snap port`
marks most changed sites (grep for it), but not all of them.

### N64ModernRuntime

* `ultramodern/src/threads.cpp` -- pooled host threads, the replenisher, and
  the per-guest-thread run clock.
* `ultramodern/src/mesgqueue.cpp` -- run-clock pauses.
* `ultramodern/include/ultramodern/ultramodern.hpp`, `librecomp/src/pi.cpp`,
  `librecomp/src/rsp.cpp` -- the matching interface and I/O changes.
* `ultramodern/include/ultramodern/input.hpp` -- `Pak::ControllerPak`
  uncommented, so a port can report a pak that the rumble path does not
  claim (the Snap Station on port 4, `src/snap_station.cpp`).

### RT64

Against rt64/rt64 `a012a23`, 69 of the 301 tracked files differ (blob hash
comparison of 2026-09-05 against the index's blob ids and
`git ls-tree -r a012a23`, repeatable that way).

**Forty-six files carry the marker**: forty-five under `lib/rt64/src` and
`lib/rt64/include/rt64_extended_gbi.h`. Forty-one are modified upstream
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
`shaders/TextureCopyPS.hlsl`, `shared/rt64_texture_copy.h`), the shader
seen-list warmer (`render/rt64_raster_shader_cache.cpp`,
`render/rt64_shader_blob_cache.h`), the Jynx recolour
(`render/rt64_snap_recolor.h`), the depth of primitive-depth sprites and the
per-call parameters the pixel stage reads (`shaders/RasterPS.hlsl`,
`shaders/RasterVS.hlsl`, `shared/rt64_rdp_params.h`,
`shared/rt64_framebuffer_params.h`), the Snap Station's overlay
(`hle/rt64_snap_overlay.h`), the port's diagnostics header
(`hle/rt64_snap_diag.h`), and configuration fields
(`common/rt64_user_configuration.h`, `rt64_enhancement_configuration.h`).

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

### plume (inside the ignored contrib tree)

Three files differ from plume `51b1ad4`, none of their contents exists
anywhere in plume's history, and all three are **force-tracked** in this
repository (`git add -f`; the directory around them stays ignored):

* `plume_d3d12.cpp` (tracked since commit `7d704d4`, which was `35bcba0`
  before the history rewrite of 2026-09-02). It carries three changes: the
  null guard in `setSamplePositions` described below; `D3D12SwapChain::present`
  keeping a sync interval of 1 when vsync is on even with present wait (the
  comment in the file says why); and a Direct3D 12 pipeline-library
  persistence layer (`hashGraphicsPipelineDesc`,
  `D3D12Device::setPipelineCacheData` / `getPipelineCacheData`, the
  `snap_pipeline_reused` / `snap_pipeline_built` counters) that
  `lib/rt64/src/hle/rt64_application.cpp` drives.
* `plume_d3d12.h` (tracked since 2026-09-02): ten added lines, the
  `pipelineLibrary`, `pipelineLibraryBlob` and `pipelineLibraryMutex` members
  of `D3D12Device` and the two overrides.
* `plume_render_interface.h` (tracked since 2026-09-02): six added lines, the
  virtual `setPipelineCacheData` / `getPipelineCacheData` on `RenderDevice`
  with default bodies that report no support.

The `.cpp` does not compile against pristine plume headers, which is why the
headers are tracked too (until 2026-09-02 they were not, and the only copy of
their changes was the developer's working tree). `tools/fetch_deps.py` clones
plume at the pin, which writes upstream's versions over the three files, and
then puts them back with `git checkout --`; an uncommitted local edit to one
of them is kept and reported instead.

The file that used to document the null guard
(`lib/rt64/src/contrib/PLUME_PATCHES.md`) sits in the ignored directory and
is not in a clone, so the substance is repeated here:

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
before the copy; a buffer destination needs the same reset. **Re-vendoring or
updating plume silently reverts all three changes**; whoever updates plume must
re-apply them or confirm upstream has an equivalent. The null guard is worth
offering upstream (renderbag/plume): the bug is theirs.

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
