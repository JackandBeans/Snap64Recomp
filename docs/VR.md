# Windows OpenXR VR build

This is an experimental PC VR implementation. The acceptance matrix below distinguishes implemented paths from completed headset validation. Standalone Quest execution is not supported.

## Build and run

VR releases are versioned independently of the desktop port. The current VR
version is **1.0**, defined in `cmake/Version.cmake`; desktop version updates
do not change it. See [VR changelog](VR_CHANGELOG.md). For future VR releases,
update `SNAP_VR_VERSION` (and its separate prerelease suffix, if needed) and
add a VR changelog entry. Use `vr/v<version>` for VR Git release tags to keep
them distinct from desktop tags.
Run `cmake -P tests/test_version.cmake` to verify that desktop version and
prerelease changes cannot alter the VR release number.

Follow the repository's normal dependency, ROM, patch, and recompilation pipeline first. The ROM must match the USA revision expected by `pokemonsnap.us.toml`. Never edit `RecompiledFuncs` directly: add wrappers to `tools/hook_funcs.py`, change patch sources, then regenerate.

```powershell
python tools/fetch_deps.py
python tools/hook_funcs.py
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64 -DSNAP_ENABLE_VR=ON
cmake --build build-win --config Release --target Snap64Recomp --parallel 12
cd build-win/Release
./Snap64RecompVR.exe
```

OpenXR SDK release 1.1.53 is pinned in CMake. The active Windows OpenXR runtime must have the connected headset available on the same graphics adapter as RT64. VR forces D3D12. Builds configured with `SNAP_ENABLE_VR=ON` launch in VR by default, including when double-clicked. Use `Snap64RecompVR.exe --desktop` for desktop play; explicit `--vr` remains supported. Builds configured with `SNAP_ENABLE_VR=OFF` start in desktop mode. The build copies the hand and prop assets beside the executable; keep `assets/vr` with it.

`--vr-preview` renders synthetic stereo eyes without an OpenXR device. It saves periodic left/right PNGs in the working directory, retains desktop controls, and is intended for diagnostic use. It cannot establish comfort or headset alignment.

Packaging the Release build with `cpack -C Release` from its build directory
produces `Snap64RecompVR-<version>-win64.zip` and a SHA-256 checksum. The
archive includes runtime DLLs, VR assets, instructions and license notices.
No ROM or player saves are packaged. The CMake build target remains `Snap64Recomp`.

## Controls

The original game's Options entry and pages are hidden in VR, including the
title and pause-menu entries and the Esc/Select shortcut. The VR settings
panel remains available on the left thumbstick. `--desktop` restores the
original Options interface.

Validation: a synthetic VR title capture showed New Game and Continue with
Options omitted; scripted desktop menu shortcuts did not open the pages.
The Release build passes. The pause-menu entry is disabled at creation and
excluded from navigation; a fresh pause-menu headset check remains outstanding.

- Menus: dominant-hand thumbstick changes the highlight; trigger or A/X confirms, B/Y cancels. The left controller menu button sends Start. Point directly at the desired control. Title, options and pause use sprite/row hit tests; original lab and photo menus move their focus rectangle to the pointed target and defer confirmation until it arrives. On-screen A/B prompt icons and their adjacent labels are clickable. Name entry maps the pointer directly to each character and to Backspace, Space and End; the pointed key is latched when trigger or A/X is pressed.
- VR options: click the left thumbstick to open the settings panel. Point at a row and press trigger or A/X, or use the thumbstick to select and adjust. B/Y or Done closes it. Opening it during a course requests pause.
- Recenter: press both thumbsticks together, or F9 on the PC. Sit or stand in the intended posture and look forward when recentering.
- Camera: squeeze grip near the holster on the right to pick it up with either hand. Hold grip to carry it; release to return it. The holding hand's trigger takes one photograph per press; its thumbstick changes lens zoom. Grip with the free hand near the camera to steady its orientation.
- Items: grip near the left apple dispenser or right pester-ball dispenser, then release grip to throw. Items appear only after their original unlocks and cooldowns. Tracking loss cancels held objects rather than throwing them.
- Course: B/Y holds dash; A/X advances dialogue. Press an empty hand into the round flute-icon button built into the left dashboard; contact with the visible fingers or palm activates it without pulling a trigger. It stays gray and inactive until the original Poke Flute unlock. Once unlocked it plays for 10 seconds; the green rim shows time remaining. Withdraw and press again to change tune and restart the timer. Throws do not interrupt it; pause, focus loss, and cutscenes cancel playback. The left controller menu button pauses where the runtime makes it available.
- Tutorials: a binocular panel gives the current grip/shutter instruction. A/X continues informational prompts. The panel stays visible during the original text's blink interval and clears when the tutorial dismisses it. Head/camera turns count as looking around without requiring desktop mouse capture.

The cart follows route position and yaw; its pitch and roll never rotate the tracking origin. Physical leaning and turning are retained. There is no free locomotion.

## Settings and saves

VR settings live in `snapsettings.json` beside the executable:

| Key | Default | Meaning |
| --- | --- | --- |
| `vr_left_handed` | `false` | Dominant hand for menus |
| `vr_eye_height` | `1.2` | Calibrated eye height above the cart origin, meters |
| `vr_render_scale` | `1.0` | Multiplier on OpenXR's recommended eye dimensions |
| `vr_throw_strength` | `1.0` | Multiplier on tracked release velocity |

The in-headset VR options panel edits the same values. Handedness, height and throw strength apply live; render scale applies on the next launch. Direct file edits require restarting. Recenter is available during play. The physical holster stays on the right and can be grabbed with either hand.

Original save data remains `saves/pokemonsnap.bin`. VR photographs also need `saves/vr-photo-lenses.json`: the original photo record stores eye and target but cannot represent arbitrary lens roll or variable field of view. Keep this sidecar with the save. Its metadata also applies when viewing those photographs in desktop mode.

## Implementation boundaries

`src/vr/vr_openxr.*` owns session, actions, predicted poses, stereo swapchains and submission. `vr_interaction.*` contains the cart-relative pose and interaction state machine. `vr_game_bridge.cpp` wraps guest functions at the verified ROM revision's addresses. `vr_renderer.cpp` replays perspective draw calls from one simulation snapshot into separate eye and lens targets; it excludes framebuffer side effects. `vr_props.cpp` draws world-depth-tested accessories after the world. These accessories never enter the game's photograph detector or scoring renderer.

Off-axis Pokemon draws are added without allocating photograph detector regions. The main guest camera follows the handheld lens. Detector metadata carries source-frame IDs, and lens projection/up-vector metadata is restored during photo review and scoring.

Eye passes use RT64's matched world matrices and vertex/UV interpolation weights at the runtime refresh rate. Cart snapshots are associated with game workloads and interpolated over the same interval, with shortest-path yaw and course-block origin rebasing. Tracking remains predicted each display frame. The lens/scoring image retains its simulation snapshot. Desktop interpolation settings are preserved.

Sky adjustment is restricted to the actual `SkyBoxObject`, resolved through the world overlay. Several terrain blocks reuse `drawSkyBox2Cycle`; they must retain their original transforms. The dome follows the cart independently of the handheld camera. Eye decal tolerance uses floating-point depth precision instead of the N64 compressed-depth tolerance.

The camera, ZERO-ONE-inspired vehicle, apple and Pester Ball are authored in `tools/build_vr_models.py` and `assets/vr/snap_vr_props.blend`; regenerate with `blender --background --python tools/build_vr_models.py`. The exporter validates transforms, normals and triangle area, and writes indexed meter-scale geometry with UVs to `props.json`. `vr_props.cpp` loads all four meshes for the cockpit, dispensers and hands. `tools/vr_projectile_assets.py` also exports the apple and Pester Ball to `projectiles.bin`: the same geometry goes through the game's F3DEX2 renderer for released items, the viewfinder, and photo reconstruction. The VR hook temporarily replaces only the matching item's render tree, leaving movement, collision, shrink, deletion and scoring logic intact. The immutable display-list/vertex arena is reserved at 0x80F00000..0x81000000, above the UI heap and below the mod heap. Regenerate the binary after editing `props.json`; the Blender exporter does this automatically. Run `python tests/vr/test_projectile_assets.py` to validate the exported batches, triangle counts and meter scale. `tools/vr_assets.py` converts the local DramaticShape numeric hand mesh, skeleton and poses into `assets/vr/hands.json`; attribution is in `assets/vr/NOTICE.md`.

The polished Blender props retain the camera origin, palm sockets, live screen plane and item reach locations. Camera details include a ribbed focus ring, recessed lens, front casting, flash diffuser, accessory shoe, strap eyes and grip ribs. The ZERO-ONE adds an interior liner, hull trim, seat panels, gauge graduations/needles, wheel rim hardware, lamp details and supported item wells/holster. Details use geometry and colors supported by the runtime. The camera is 10,436 triangles and the vehicle 31,596, down from 24,300 and 37,940 respectively; this is a geometry reduction, not a measured headset performance claim.

The `.blend` has `camera`, `zero_one`, `apple` and `pester_ball` collections, a `VR Props Source` scene, and eight review scenes. Regeneration writes front/rear/side camera, vehicle/cockpit and individual item renders (`build-win/vr-*-review.png`) plus `build-win/vr-model-stats.json`. Export also checks finite attributes and triangle area after rounding and enforces triangle budgets. The item integration and transparency support require rebuilding the executable; the build copies the matching assets into `build-win/Release/assets/vr/`. For later geometry-only edits, copying the regenerated assets there is sufficient.

The apple has a lobed peel with vertex-color blush, recessed crown, curved stem and solid folded leaf (2,980 triangles). The Pester Ball has three separate, raised red/blue/orange shells over a smaller yellow core, with recessed yellow channels and inset purple ports (9,048 triangles). The panel crowns sit 4.5 mm above the inner sphere. The channels are now approximately 2.5 times wider (0.28 radians between panels), and the colored shells use 65% opacity. Both retain the existing center-origin hand placement and roughly 55 mm body radius. Individual portable exports are `assets/vr/apple.glb` and `assets/vr/pester_ball.glb`; the game uses their geometry in `props.json`.

Transparent models export an optional per-vertex `alpha` array alongside the existing 11-float vertex layout. The accessory renderer draws opaque geometry first, then alpha-blended triangles sorted back-to-front separately for each eye, with depth testing enabled and transparent depth writes disabled. The yellow core and purple ports remain opaque. The GLB also preserves the colored materials' opacity.

Polish validation: the camera and vehicle Blender views were inspected, and 130 ray samples across the live display rectangle confirm no model geometry in front of its plane. The earlier runtime preview reached the lab and exited before a course capture. The item addition includes reviewed Blender renders, closed-mesh and portable-export checks, a Windows Release build and passing existing VR interaction tests. A synthetic preview initialized with all four models. In-course and headset item appearance remain unverified.

Camera attachment follow-up: reviewed the three Quest screenshots `VirtualDesktop.Android-20260923-155300.jpg`, `-155310.jpg`, and `-155318.jpg`. Added an eyecup neck and rear control rail, extended the lens barrel, and seated the optical layers, markings, screws and flash ribs. `tools/vr_model_validation.py` now checks actual surface intersections/containment from every camera component to the main casting; all 83 parts connect. Saved-source validation also passes all closed-mesh, display-clearance, widened-channel and opacity checks. Portable exports retain the expected triangle counts and translucent materials. VR tests cover transparent ordering, including separate stereo-eye order and transformed viewers. These changes still need a fresh headset appearance check.

## Validation record (2026-09-23)

- Released-model follow-up: an isolated Beach replay with `SNAP_VR_PROJECTILE_TEST=1` visibly renders both authored item meshes in the world in both eye captures. The diagnostic is preview-only and periodically spawns real game items; it is not enabled in normal VR. Export validation checks the checked-in binary against the mesh source, every vertex batch and triangle index, and physical scale. Release build and interaction tests pass. Photo reconstruction uses the same matched item render tree, but an end-to-end photo review containing these items has not yet been visually checked.

- Apple and Pester Ball releases now use DramaticShape's VR Pokeball peak-window estimator (35-100 ms segments within 120 ms), with its 3x velocity boost above 0.9 m/s. Sampling the visible held item's center includes wrist rotation and avoids a spawn jump. Throw strength applies to the gesture before adding cart velocity. Snap retains its original projectile gravity and collisions; slow releases remain drops, and no single-target magnet or catchable-juggling mode is introduced. Tests cover flick slowdown, stale peaks, short samples, both item types, rotated cart motion, and rendered/spawn position agreement. Physical feel still needs controller testing.

- Left-eye ghosting follow-up: corrected the OpenXR color swapchain copy barriers from COMMON to RENDER_TARGET on acquisition and release, as required by XR_KHR_D3D12_enable. The copy fence still completes before release. This fixes an API contract violation; whether it resolves the reported ghosting requires headset verification. Creating `vr-capture.request` in the running game's working directory captures both actual acquired swapchain images as `vr-submitted-left/right.png` before release, then removes the request. Capture readback stalls make this a diagnostic operation only.
- Head-motion ghosting follow-up after VR 1.0: actual submitted logo/title images showed no duplicate edges, while SteamVR logged 5,697 reprojected frames out of 11,971 presents. The earlier swapchain/stereo fixes remain present. VR mirror presentation now skips desktop VSync, software pacing and the display wait so OpenXR controls timing. Runtime application rates below 60 Hz are preserved instead of being forced to 60, avoiding extra interpolation work when the runtime throttles. These correct timing defects; resolution of the reported left-eye ghosting still requires a headset comparison. `Launch-VR.cmd` now uses `Snap64RecompVR.exe` instead of a stale `Snap64Recomp.exe`.

- Distant stereo follow-up: eye passes bypass RT64's desktop 4:3 scissor-driven horizontal adjustment. Camera zoom could activate this adjustment, stretching asymmetric OpenXR projections and introducing disparity even at infinity. Eye viewport depth now uses the full 0..1 range consistently with accessories. Desktop and lens passes retain their original viewport rules.
- `SNAP_VR_STEREO_TEST=1` with `--vr-preview` exercises mirrored asymmetric FOVs at 960x1020 per eye (the Quest's observed aspect ratio). A held-camera Beach replay reached zoomed 180x140 source viewports; both captures were inspected and diagnostic checks found no drawn vertices using an unreplaced mono projection. Release build and interaction tests pass. This is synthetic validation; the reported distant doubling still needs headset confirmation.

- Follow-up: held-camera body and optical pose rotate 90 degrees forward together while preserving either palm socket; unit tests cover both hands and two-hand steadying.
- Follow-up: a fresh-game Beach tutorial was reached through VR controller input in an isolated data directory. Both eye captures visibly show the grip tutorial panel (`build-win/vr-fresh-tutorial/vr-preview-10800-left.png` and `-right.png`). The ROM message string observed at this step was empty, so tutorial-specific VR text comes directly from the original tutorial message ID. Later tutorial steps and physical-controller fit still need headset confirmation.
- Follow-up: diagnostic preview logs show three world/cart samples per 30 Hz update at 90 Hz (weights 0.1667/0.5000/0.8333 or 0.3333/0.6667/1.0000). Typical Beach VR-pass CPU time was 5–7 ms at 1280x960 per eye; this excludes the original game pass and is not a native-headset performance result. Button pulses span a simulation tick, and menu triggers retain their held state.

- Desktop Release and OpenXR Release builds succeeded on this PC. The reconstructed USA ROM matched SHA-1 `edc7c49cc568c045fe48be0d18011c30f393cbaf`.
- Standalone pose/interaction tests pass (including independent headset/camera aim and stationary drops inheriting cart velocity): yaw-only cart transform, lean parallax, recenter, asymmetric projection, camera grab/shutter/return, film exhaustion, unlock/cooldown gates, both-hand item acquisition, release velocity, tracking/focus loss and course transition cancellation.
- Synthetic stereo captures reached the Beach course from a fresh game through title, name entry, lab and course selection. The course, cart, hands and live camera display rendered.
- A scripted pointer test entered AZ, deleted Z, added a space and selected End through the original name screen; the name and highlight were checked in rendered captures. All 95 character-grid hit targets and editing buttons have mapping tests. Both hand meshes have a 90-degree forward wrist correction requested during headset testing.
- Quest via SteamVR reached the focused OpenXR session state and submitted frames using sRGB RGBA8 swapchains. Runtime-requested eye size was 3760 x 3996 at scale 1.0.
- Initial live menu stereo rendering measured approximately 10-12 ms of CPU wall time including synchronous GPU waits, excluding `xrWaitFrame` and the original game's render. This is **not** a measured total GPU frame time or proof of refresh-rate performance.

Revision after headset screenshots (2026-09-23):

- Reviewed the three September 23 ADB screenshots, not the video. Replaced the block camera and cart with a detailed side-grip camera (24,300 triangles) and a rounded ZERO-ONE-inspired cockpit (37,940 triangles). Both eye views reuse one accessory geometry snapshot. In the isolated 1280x960 Beach check this reduced the VR CPU pass from about 12.7 ms to 5.1?5.3 ms; that includes waits, excludes the original game pass, and is not Quest-resolution performance validation.
- Camera body attaches its left/right side socket to the corresponding palm; the lens follows the controller aim pose. Synthetic Beach eye captures show the display unobstructed by the holding wrist.
- Eye passes use floating-point depth without the N64 far-depth clipping/quantization. Fog retains the original camera-distance curve. The lens/photo pass retains the original depth behavior. VR sky drawing follows the lens during cinematic introductions, preventing the dome from remaining around the displaced cinematic camera. A captured Beach stereo pair was inspected; this does not establish headset comfort or cover every distant object.
- Pointer testing in an isolated copy of the save held Save steadily, then opened Go to Course on a simultaneous pointer move/trigger press. Title ? Options ? Graphics and row help updates were also checked. Pointing at Maybe later returned to course selection; pointing at Let's go entered Beach. The original neutral-input API is preserved for frozen confirmation menus; button-image hooks expose on-screen A/B prompts as pointer targets.
- Tests include row/grid hit targets, no scrolling over a selected target, and camera palm sockets/aim alignment for both hands. `SNAP_VR_POINTER_TEST` (preview only) reads four whitespace-separated numbers from a file: panel X, Y, trigger 0/1, back 0/1. `SNAP_VR_MODEL_TEST=1` adds a synthetic held-camera pose in courses.

Still requiring verification: headset comfort and stereo alignment; independent-camera photos through Oak scoring; physical item collisions/reactions and moving-cart throws; pause/retry, runtime restart and save/load; every course and unlock-dependent interaction. No complete-game or performance acceptance is claimed.

The runtime log now reports CPU wall time, D3D12 timestamp queue span, and synchronous fence-wait time for the VR passes, alongside the runtime-selected refresh rate. Queue span includes CPU submission gaps; all three metrics exclude the original game pass and its readbacks. Set `SNAP_VR_CAPTURE=1` to save one pair of actual headset eye images after 120 focused frames.

Run the pure interaction tests with:

```powershell
cmake -S tests/vr -B build-win/vr-tests -G "Visual Studio 17 2022" -A x64
cmake --build build-win/vr-tests --config Release
ctest --test-dir build-win/vr-tests -C Release --output-on-failure
```


September 23 interaction and cinematic revision:

- Both dispensers remain on the right. Pester balls are 25 cm forward of bait; both moved inward/up for seated reach. Grab arbitration chooses the nearest eligible prop.
- Throw release now fits the last 80 ms of tracked item-center motion, with a smooth gain capped at 2.3x instead of the previous 3x peak boost. Tests cover 72/90/120 Hz consistency, slow releases, spikes, tracking loss and cart velocity.
- Original pester-ball impacts start a depth-tested purple smoke puff. Both projectile types retain their native 3D replacements and original collision/effect paths.
- Course cinematic/ride changes hold and fade out the outgoing view over 250 ms, switch at black, and fade in over 400 ms. Native cart accessories are hidden during authored cinematics.
- The opening movie uses tracked stereo behind a room-space vignetted portal. Its cloud sky mesh and culling bounds are expanded fourfold about their center; an unbounded fog-color backdrop fills any remaining uncovered pixels. This is restricted to the opening overlay and sky payload, preserving desktop assets and foreground geometry.
- Release build, interaction tests and projectile asset checks pass. Preview rendering has been inspected; physical throw feel, full exit-gate transitions and comfort still require headset validation.


Follow-up presentation and handedness fixes:

- VR menu panels consume RT64's selected presentation target after cut holds, including interpolation targets. A private copy retains the last UI through photo-processing workloads without a display image, instead of submitting an empty OpenXR layer. Full-height UI fallback excludes small offscreen photo buffers.
- Cinematic eyes compose tracked eye motion with RT64's interpolated authored camera transform. VR enables camera interpolation independently of the desktop toggle; matched world animation interpolation remains active and cut detection is retained.
- A left-held camera reflects only body geometry across its local X axis, with corrected triangle winding and the mirrored right-hand grip socket. The screen position follows the mirrored bezel, while screen UVs, focus ring and film digits remain readable. Right-held and docked cameras retain their original geometry.
- Release build and palm-attachment tests pass. Preview logs show changing intermediate cinematic view transforms. A left-hand Beach capture shows the red shutter on the left, with upright viewfinder imagery and readable film digits. A complete tally countdown and headset comfort still need runtime confirmation.


Shutter contact and rear visibility follow-up:

- Reviewed Quest screenshot `VirtualDesktop.Android-20260923-201716.jpg`. Moved the shutter/collar 19 mm inward onto the top casting. The exported shutter vertex mask drives 2.5 mm of trigger travel without moving the collar.
- The held-camera index chain now targets the physical shutter cap, including mirrored left-handed placement. The fingertip follows trigger travel; other fingers keep the imported grip pose.
- VR display-list bounds retain triangles outside the original camera's view, including rear-facing Pok?mon, for per-eye clipping and depth testing. The original lens-frustum photo subject selection and game-managed despawning are unchanged.
- Blender assembly/contact and geometry validation, projectile asset validation, Release build and interaction tests pass. A right-hand preview shows the finger over the shutter; actual controller fit remains a headset check.

The rear-facing synthetic capture renders the scene, but the preview exited before Pikachu-specific visibility could be confirmed. Verify that encounter in the headset; no full-course visibility acceptance is claimed.


Item-grip follow-up:

- Bait and pester balls now use DramaticShape's full `grip_4` pose while held, independent of trigger/thumb input. Only item-holding hands use its wrist nudge; the camera grip stays as fitted separately.
- Palm placement and per-hand item orientation come from the local `HandProp.lua` and `data/ball_grip.lua`. Visible item centers, motion-history sampling and projectile release positions share the same mirrored transform.
- Interaction tests verify both palm seats, outward-facing item orientation, and release/render center agreement; Release build passes.

The five item tutorial page replacements and exact new wording are documented in [VR control text replacements](VR_CONTROL_TEXT_REPLACEMENTS.md).

Flute dashboard follow-up:

- Reviewed Quest screenshot `VirtualDesktop.Android-20260923-212443.jpg`. Replaced the square text button with a shallow round cap and silver bezel in a rounded extension of the dashboard. The cap displays the original game's flute HUD icon, decoded from the user's loaded ROM at runtime; no game icon pixels are included in the repository or accessory assets.
- Either rendered hand can physically depress the cap without a trigger pull. Contact uses the same animated hand geometry as rendering, and the hand must withdraw before another press. The button and icon stay gray and inactive until the original game unlocks the flute.
- Release build, interaction tests and projectile asset checks pass. Tests cover both hands, trigger-only rejection, held contact, tracking recovery, locked input and icon decoding. Synthetic Beach previews showed two physical presses with zero trigger input, music stopping about 10 seconds after the second press, and no playback in locked mode. Unlocked stereo captures and the locked appearance were inspected. Physical reach and press feel still require a headset check.

Audio delay follow-up:

- The generated audio loop still read queue depth at `0x80700004`, while the host published it at `0x80C00040`. This made synthesis overfill the playback queue. Both inlined `AI_LEN` reads, including audio reset, now call the host directly through `tools/hook_funcs.py`; the graphics-thread mailbox publication has been removed.
- In an isolated 1280x960 stereo replay, periodic pre-submission audio queue samples reached 884 ms before the fix, triggering the one-second overflow reset. After the fix, 26 samples stayed between 7 and 11.5 ms with no resets. Fourteen successful synthetic shutter presses reached `makePhoto` in 3.9–14.3 ms; photo creation and lens-metadata persistence are timed separately. These are host measurements, excluding audio driver, headset transport and speaker latency.
- Release build and interaction tests pass. `SNAP_VR_LATENCY_DIAG=1` logs input-to-poll, input-to-photo, photo-save time and audio queue depth. For isolated replay testing, combine `--vr-preview`, `SNAP_VR_MODEL_TEST=1` and `SNAP_VR_LATENCY_TEST=1`: recorded input navigates menus, then synthetic hand triggers drive course input. This mode must use a separate `SNAP_DATA_DIR` because it takes real in-game photos.
