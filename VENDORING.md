# Vendored dependencies

`.gitmodules` declares `lib/N64ModernRuntime` and `lib/rt64` as submodules, but no
gitlink was ever committed for either — `git ls-files -s` returns no mode-160000
entries in any commit, so `git submodule update --init --recursive` is a no-op and a
fresh clone cannot configure. This file records what is actually needed, until that
is fixed properly.

## Recovered pins

These two still had live `.git` directories when this file was written, and their
identities were read out before that could stop being true:

| Path | Upstream | Commit | Tag |
| --- | --- | --- | --- |
| `lib/SDL` | https://github.com/libsdl-org/SDL.git | `fa24d868ac2f8fd558e4e914c9863411245db8fd` | `release-2.30.11` |
| `lib/DirectX-Headers` | https://github.com/microsoft/DirectX-Headers.git | `ee479f0bd5f7b884f202bcf0c3f076cc050dd256` | `v1.619.5` |

## Lost pins

`lib/rt64/.git` is an orphaned gitfile pointing at `.git/modules/lib/rt64`, which no
longer exists, so `git -C lib/rt64 status` fails. The recorded commits of rt64's own
contrib submodules lived only in that deleted gitdir and are **not recoverable from
this repository**. The working-tree contents are the only remaining copy:

`ddspp`, `dxc`, `hlslpp`, `im3d`, `imgui`, `implot`, `json`, `miniz`,
`mupen64plus-core`, `mupen64plus-win32-deps`, `nativefiledialog-extended`,
`plainargs`, `plume`, `project64`, `re-spirv`, `spirv-cross`, `stb`, `utf8conv`,
`xxHash`.

Reconstructing them means matching each directory's contents against upstream tags.
Until then, `lib/rt64/src/contrib` must be preserved as tracked content rather than
ignored — losing that working tree loses the build.

`lib/N64ModernRuntime` likewise has no git directory. It carries local modifications
(the pooled host threads and the per-guest-thread run clock in
`ultramodern/src/threads.cpp`, and the message-queue run-clock pauses), so it cannot
simply be re-cloned from upstream either.

## Local modifications to vendored trees

Both vendored trees are **forked, not pristine**. Anything that re-syncs them from
upstream must preserve:

- `lib/N64ModernRuntime/ultramodern/src/threads.cpp` — pooled host threads,
  replenisher, per-guest-thread run clock; `mesgqueue.cpp` — run-clock pauses.
- `lib/rt64/src/hle/rt64_state.cpp`, `rt64_workload_queue.cpp`,
  `rt64_present_queue.cpp`, `render/rt64_raster_shader_cache.cpp`,
  `shaders/TextureSampler.hlsli` — object identity, frame pacing, the shader
  seen-list warmer, and the interpolation span work. Search these for
  `Pokemon Snap port` to find every one.
- `lib/rt64/src/contrib/plume/plume_d3d12.cpp` — force-tracked inside an otherwise
  ignored tree, carrying a readback fix (commit 35bcba0).
