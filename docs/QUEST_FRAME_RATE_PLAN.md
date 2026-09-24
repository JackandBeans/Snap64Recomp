# Quest frame rate and animation implementation plan

Status: proposed next implementation, following measured baseline `c0e9528`.
The current build has not passed 80 Hz or 72 Hz qualification.

Deliver newly rendered gameplay **and interpolated Pokemon animation at 80 Hz**,
with 72 Hz as an explicitly reported fallback. Head tracking, compositor refresh,
or changing interpolation counters alone cannot satisfy this requirement.
Keep 100% recommended eye resolution, normal game speed, all scene content,
photo scoring, held-camera response, and existing sky/tutorial fixes.

## What the measurements and code establish

The retained device run `artifacts/quest/20260924T213628Z-1cff958/` measured
60.29 application FPS, 30.01 source FPS, 1680x1760 per eye, confirmed 80 Hz,
and 24.64% missed intervals over 80.61 seconds of Beach. It recorded 2,442
additional submissions with changed interpolation weights on the same workload.
Those weights establish extra interpolation samples, not proof that every
Pokemon's rendered pose changed correctly. This was a halfway iteration, not
full qualification. The separate preceding capture run is not a timing baseline.

Median VR CPU elapsed time was 11.77 ms, including 7.59 ms of fence waits.
The 10.36 ms GPU queue span includes gaps between submissions and excludes the
original game pass. These numbers overlap: subtracting 7.59 ms does not establish
7.59 ms of removable GPU work, and adding them is not total frame cost.

Concrete remaining dependencies:

- `WorkloadQueue::threadRender` calls `snap_vr_render` inside the source-workload
  interpolation loop. Its source deadline can discard remaining display samples.
- `Props::endTiming` drains the GPU every headset frame, before `xrEndFrame`.
- `Renderer::replay` still temporarily swaps buffers into mutable `Workload`
  objects. Moving this function onto another thread unchanged would race.
- Per-view RSP processing and world drawing still execute separately for each eye.
- The Vulkan backend creates pipelines with a null explicit `VkPipelineCache`.
  The shipped list of known shaders is not a persisted Vulkan pipeline cache.

The working hypothesis is that source-coupled scheduling and synchronous frame
completion prevent a steady 80 Hz animation stream. Actual GPU throughput may
also be insufficient; the next instrumentation must distinguish both costs.

## Implementation sequence

1. **Measure complete frame cost before changing scheduling.** Add buffered
   timestamps for source matching/upload, original framebuffer/scoring passes,
   world preparation, both eyes, viewfinder, props, image transfer, queue submit,
   lock waits, and XR wait/begin/end. Read GPU queries only after their owning
   frame has completed. Separate GPU execution intervals from queue idle gaps.
   Record source simulation time, publication time, selected animation time,
   snapshot IDs, frame slot, late-source count, and physical/app refresh.
   Extend the runner to retain the installed APK identity and immutable telemetry
   from the same measurement interval. Use repeated halfway runs for comparisons.

2. **Give each in-flight headset frame complete ownership of its resources.**
   Introduce two bounded frame slots containing all three views' command buffers,
   descriptors, uploads, RSP outputs, render targets, props indices/vertices,
   query storage, and referenced resource handles. At reuse, wait only for that
   slot's completion fence. Retain textures, geometry, framebuffers, and pipeline
   handles until the fence retires; remove the mutable `ViewBorrow` mechanism.
   Group command recording/submission where dependencies allow it. Do not reset
   descriptors, mutate shared image-layout bookkeeping, or release cache pins
   merely because CPU recording finished.

   Release XR images after the required commands and layout transitions have
   been submitted to the graphics-binding queue, following the Vulkan binding's
   image-state contract. Retire application resources on completion separately.
   Use the existing Vulkan queue mutex for submissions and XR calls that may
   access that same queue. Keep `xrWaitFrame` outside it. Split the current
   combined wait/begin helper accordingly. Do not hold workload locks while
   waiting for a slot, swapchain image, or headset pacing.

3. **Publish immutable source snapshots and schedule XR independently.** Keep
   guest simulation and required framebuffer/scoring work at their existing
   source cadence. Publish completed render descriptions with stable transform
   IDs, timestamps, epoch/cut/rebase metadata, vertex matching/velocities,
   materials, RDP tiles/look-at data, immutable geometry, and texture versions.
   Source uploads need an explicit GPU-ready dependency; CPU publication alone
   does not make a buffer ready for reading.

   Use a bounded latest-snapshot mailbox and a small pool with explicit reference
   ownership. Retain the interpolation pair plus snapshots pinned by in-flight
   slots. Replace obsolete unpublished render snapshots instead of accumulating
   a FIFO; never skip guest simulation or required photo/scoring work. On pool
   pressure, reuse the latest complete presentation state, record stale age,
   and fail sustained overload rather than growing memory or animation latency.

   A dedicated XR render loop owns XR frame ordering, predicted-time pose
   sampling, and frame-slot submission. It consumes snapshots without borrowing
   the producer's mutable `Workload` or holding simulation locks. Required GPU
   work may share a queue; independent CPU threads do not imply extra GPU power.
   Start with one pending GPU frame while the CPU prepares the next, and measure
   queue depth rather than filling both slots with old frames.

4. **Interpolate Pokemon animation for every predicted display time.** Convert
   source simulation timestamps and OpenXR predicted display time to one stable
   clock domain, anchored to actual source progression. For a 30 Hz source, use
   a fixed one-source-frame presentation delay (about 33.33 ms). Select the
   bracketing pair A/B for `animationTime = predictedDisplayTime - delay`, then
   evaluate `alpha = clamp((animationTime - A.time)/(B.time - A.time), 0, 1)`.
   Reject zero/nonmonotonic intervals. Do not derive alpha from the number of
   headset frames that happened to fit inside a source job.

   Apply the existing transform matching and interpolation semantics to Pokemon
   body/limb transforms **and matched deforming vertices**, including normals and
   other animated attributes where supported. Preserve per-transform cut flags,
   origin rebasing, births/deaths, topology changes, and texture animation rules.
   Audit species whose animation currently bypasses matching; camera/cart motion
   alone is insufficient. Reuse source geometry/material preparation, but
   evaluate the animation pose at every 80/72 Hz sample and share it across eyes.
   A held viewfinder uses the same animation time with the latest controller
   pose; a docked viewfinder may remain at source cadence. Guest photographs and
   scoring remain authoritative at source time and must never read interpolated
   headset targets.

   Keep selected animation time monotonic within an epoch. Missing snapshots
   clamp to the newest valid pose without extrapolating game state, banking work,
   or lengthening the presentation delay. Record the resulting hold as a miss.
   Resume from current predicted time when data returns; do not replay a backlog.
   Flush pairs on session/epoch reset. Handle camera cuts per transform so they
   do not freeze unrelated Pokemon animation. Head and hand tracking remain
   current; the source-animation delay does not delay tracked input.

5. **Reduce actual GPU work if it still exceeds budget.** The 80 Hz budget is
   12.5 ms; 72 Hz is 13.89 ms. Include amortized guest work in the throughput
   calculation: `displayHz * headsetGpuMs + sourceHz * guestGpuMs < 1000`, with
   headroom for compositor activity and thermal variation. This is a diagnostic
   necessary condition, not proof of frame-deadline success.

   If stereo preparation/world drawing exceeds half the frame budget after
   batching, add capability-gated Vulkan multiview. Use a two-layer color/depth
   target, view mask 0b11, per-eye transforms, and view-index-aware RSP/raster
   outputs; updating only the render-pass mask would reuse the wrong eye's
   projected vertices. Share world pose/lighting preparation. Initially copy
   the two layers to the existing XR swapchains to preserve their working
   format/occlusion path. Retain sequential stereo as a fallback. Multiview
   reduces duplicated setup/draw work; it does not halve fragment shading.
   If fragments or transfers dominate, optimize those measured passes without
   lowering resolution or removing content.

   Add a persisted Vulkan pipeline cache keyed by device/driver/cache UUID and
   application shader identity, with safe invalidation and atomic replacement.
   Prepare known Beach variants before warm timing, while reporting cold-start
   compilation stalls separately. Keep cache behavior identical in production
   and benchmark builds.

## Tests and acceptance gates

- Test 30-to-80 and 30-to-72 *pose evaluation*, not only pacing counters: analytic
  limb rotation and vertex motion sampled at predicted times, fractional cadence,
  missing sources, refresh changes, per-object cuts, rebasing, pause/resume, and
  recovery with bounded age and no backward animation time.
- Test delayed GPU completion, descriptor/upload reuse, source-pool exhaustion,
  texture replacement, session recreation, and teardown while frames are pending.
  Run Vulkan validation separately from timed qualification.
- Track representative moving Pokemon using stable object IDs. Compare evaluated
  transforms/vertices against expected interpolated values for the chosen pair;
  count stale poses only where the source actually contains motion. Use separate
  sequential visual captures to check final rendered limb/wing/deformation motion
  and both eyes. Changed alpha or workload ID alone must never be an animation pass.
- Run repeated halfway benchmarks during development, with no capture readback.
  Track cold/warm runs separately. Fix any new sky/tutorial, camera, or scoring
  regression before pursuing a timing improvement.
- Final acceptance remains three complete Beach runs at the selected refresh,
  including one after 15 minutes of continuous play, >=99% newly rendered FPS,
  <1% missed display intervals, and no recurring animation freezes. Confirm
  native resolution, five controller-off launches, real-controller production
  behavior, and save preservation. If 80 Hz fails, qualify 72 Hz separately.
  Install the release as validated only after these checks pass.

Implementation milestones are measurement, frame ownership, independent
predicted-time animation rendering, conditional GPU optimization, and final
qualification. Each should be a separately reviewable commit with device
evidence under the ignored artifact structure. No FPS outcome is guaranteed by
the architecture change alone.

## API references

- [OpenXR Vulkan swapchain image-state contract](https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html#khr_vulkan_enable-state)
- [OpenXR Vulkan queue concurrency requirements](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_KHR_vulkan_enable-concurrency.html)
- [Vulkan multiview render-pass behavior](https://docs.vulkan.org/spec/latest/chapters/renderpass.html)
- [Vulkan view index](https://docs.vulkan.org/refpages/latest/refpages/source/ViewIndex.html)
