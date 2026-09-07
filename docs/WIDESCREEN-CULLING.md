# Widescreen visibility

With Widescreen enabled, Pokémon can remain visible outside the original 4:3
frame. The display bounds follow the current window shape, including changes
while a course is running. The setting remains opt-in.

## Why Pokémon disappeared

The renderer calculates native dirty rectangles before expanding the projection.
Pokémon Snap splits Pokémon rendering into separate framebuffer passes for its
photo detector. When a Pokémon lies entirely outside the native viewport, its
pass can have an empty native color rectangle. RT64 discarded that pass before
widescreen projection could bring the Pokémon into view.

`FramebufferPair::displayColorRect` supplies the perspective projection's scissor
as a display-only fallback for such passes in an expanded view. The workload
queue uses it when selecting passes and allocating display targets. Normal GPU
clipping then determines visibility. Native color/depth dirty bounds remain
unchanged; enlarging those also would create unnecessary RAM writeback and
photo-scoring work. Empty 2D and orthographic passes keep their original behavior.

For very wide views, the `Pokemon_GetFlag100` hook also refreshes draw eligibility
using the current camera transform and an expanded horizontal frustum. This
avoids relying on visibility flags last updated by the photo collector, whose
list has only 12 slots. The hook preserves the guest calling context and does
not modify the photo eligibility flag. Vertical and distance limits, scripted
hiding, spawn timing, and the native photo rules remain in effect.

## Outstanding work

Draw distance remains outstanding and may be addressed in a later commit. This
change fixes disappearance at the original horizontal frame edges; it does not
extend the existing distance limits or change course spawning.

## Validation

Build the normal Release executable, and opt into the focused harness:

```sh
cmake --build build-win --config Release --target SnapVisibilityTest
./build-win/Release/SnapVisibilityTest.exe
```

The harness runs 32 checks against the production framebuffer helper and guest
hook: native/expanded pass selection, viewfinder scissors, non-perspective
passes, retained RAM bounds, several aspect ratios, distance/depth/vertical
rejection, photo flags, and preservation of the guest context and stack.

Windows Release validation also used isolated profiles and deterministic Beach
replays at 4:3, 16:9, 21:9, and 32:9. Captures cover the viewfinder, turning in both
directions, and Pokémon outside the former frame edges. The 21:9 run enabled
camera interpolation. A separate captured run resized the live window through
16:9, 32:9, 4:3, and 21:9 with interpolation enabled. These runs reported no
crash or graphics-buffer overflow.
A photo regression replay produced 30 per-Pokémon scoring checks matching the
healthy buffer signature and exported 40 photos without a crash. This verifies
scoring-buffer consistency, not identical final scores between 4:3 and 21:9.
The requester also playtested the fixed widescreen build and reported that it
looks good. Other courses have not been visually revalidated with this change.

The implementation, tests, and documentation were produced with AI assistance
(OpenAI Codex).
