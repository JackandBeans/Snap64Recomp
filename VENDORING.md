# Vendored dependencies

What `lib/` actually contains, how each piece is tracked, and which pins are
known. `NOTICE.md` lists licenses; `BUILDING.md` lists what to fetch.

## `.gitmodules` is gone, on purpose

Until 2026-09-02 a `.gitmodules` declared `lib/N64ModernRuntime` and `lib/rt64`
as submodules of <https://github.com/N64Recomp/N64ModernRuntime.git> and
<https://github.com/rt64/rt64.git>. No gitlink was ever committed for either
(`git ls-tree -r HEAD | grep ^160000` is empty in every commit), so
`git submodule update --init --recursive` was a no-op and a fresh clone could
not configure. Both directories are ordinary tracked files that have been
edited in place, so the declaration was fiction and has been deleted. If they
are ever turned back into submodules, the local modifications below have to be
carried as patches on top of an upstream commit that is actually recorded.

## How each tree is tracked

| Path | In git? | State |
| --- | --- | --- |
| `lib/N64ModernRuntime` | yes, 1041 plain files | forked; no git directory; upstream commit unrecorded |
| `lib/N64ModernRuntime/N64Recomp` | yes (part of the above) | the runtime's bundled copy of N64Recomp's headers and sources; upstream commit unrecorded |
| `lib/rt64` (outside `src/contrib`) | yes, 299 files | forked; `lib/rt64/.git` is an orphaned gitfile pointing at a deleted `.git/modules/lib/rt64`; upstream commit unrecorded |
| `lib/rt64/src/contrib/` | **no** (ignored), except `plume/plume_d3d12.cpp` | 396 MB of RT64's third-party trees, pins lost (below) |
| `lib/SDL` | no (ignored) | pristine clone with a live `.git`, pinned below |
| `lib/DirectX-Headers` | no (ignored) | pristine clone with a live `.git`, pinned below |

## Recovered pins

Read from the live `.git` directories:

| Path | Upstream | Commit | Tag |
| --- | --- | --- | --- |
| `lib/SDL` | https://github.com/libsdl-org/SDL.git | `fa24d868ac2f8fd558e4e914c9863411245db8fd` | `release-2.30.11` |
| `lib/DirectX-Headers` | https://github.com/microsoft/DirectX-Headers.git | `ee479f0bd5f7b884f202bcf0c3f076cc050dd256` | `v1.619.5` |

The WSL-side tools are pinned in BUILDING.md (N64Recomp `ffb39cd`, decomp
`3a236dc`).

## Lost pins

The commits of RT64's contrib submodules lived only in the deleted
`.git/modules/lib/rt64` and are **not recoverable from this repository**. The
working-tree contents are the only remaining copy of:

`ddspp`, `dxc`, `hlslpp`, `im3d`, `imgui`, `implot`, `json`, `miniz`,
`mupen64plus-core`, `mupen64plus-win32-deps`, `nativefiledialog-extended`,
`plainargs`, `plume`, `project64`, `re-spirv`, `spirv-cross`, `stb`, `utf8conv`,
`xxHash`, `zstd`.

Reconstructing them means matching each directory's contents against upstream
tags (the single-header ones carry a version in their header: both copies of
nlohmann/json are 3.12.0). Until that is done the directory is ignored by git
and the working tree must be backed up by other means; losing it loses the
build. `dxc` is the largest piece (170 MB) and holds compiled binaries
(`dxc.exe`, `dxcompiler.dll`, `dxil.dll`, and Linux/macOS builds) whose release
version is not recorded anywhere in the tree.

## Local modifications to vendored trees

Both tracked trees are **forked, not pristine**. Anything that re-syncs them
from upstream must preserve the following; every changed site is marked with
the string `Pokemon Snap port` (grep for it).

### N64ModernRuntime

* `ultramodern/src/threads.cpp` -- pooled host threads, the replenisher, and
  the per-guest-thread run clock.
* `ultramodern/src/mesgqueue.cpp` -- run-clock pauses.
* `ultramodern/include/ultramodern/ultramodern.hpp`, `librecomp/src/pi.cpp`,
  `librecomp/src/rsp.cpp` -- the matching interface and I/O changes.

### RT64

Thirty-two files carry the marker: thirty-one under `lib/rt64/src` and
`lib/rt64/include/rt64_extended_gbi.h`. By area: object identity and
transform-group pairing (`hle/rt64_state.cpp/.h`, `rt64_rigid_body.cpp/.h`,
`rt64_transform_group.h`, `rt64_draw_call.h`, `rt64_projection.h`,
`rt64_rdp.h`, `rt64_rsp.cpp`, `gbi/rt64_gbi_extended.cpp`,
`gbi/rt64_gbi_rdp.cpp`, `include/rt64_extended_gbi.h`), frame pacing and
presentation (`hle/rt64_present_queue.cpp/.h`, `rt64_workload_queue.cpp/.h`,
`rt64_workload.h`, `rt64_game_frame.cpp/.h`, `rt64_vi.cpp/.h`,
`rt64_application.cpp`, `render/rt64_vi_renderer.cpp/.h`,
`render/rt64_framebuffer_renderer.cpp/.h`, `render/rt64_projection_processor.cpp`),
the shader seen-list warmer (`render/rt64_raster_shader_cache.cpp`,
`render/rt64_shader_blob_cache.h`), the port's diagnostics header
(`hle/rt64_snap_diag.h`), and configuration fields
(`common/rt64_user_configuration.h`, `rt64_enhancement_configuration.h`).
`shaders/TextureSampler.hlsli` was listed as modified by the previous version of this file but carries no marker and cannot be diffed against a recorded upstream; treat it as suspect.

### plume (inside the ignored contrib tree)

`lib/rt64/src/contrib/plume/plume_d3d12.cpp` is force-tracked (commit `35bcba0`)
because it carries a fix the port cannot run without, and the file that
documents it (`lib/rt64/src/contrib/PLUME_PATCHES.md`) sits in the ignored
directory, so the substance is repeated here:

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
updating plume silently reverts this**; whoever updates plume must re-apply it
or confirm upstream has an equivalent. It is worth offering upstream
(rt64/plume): the bug is theirs.

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
