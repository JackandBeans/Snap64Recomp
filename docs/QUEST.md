# Standalone Quest build

The experimental Android port builds an ARM64 Release APK with Vulkan and
OpenXR. It runs the recompiled game on the headset, without a PC streaming
runtime. The Windows PC VR build remains available separately.

## Build and install from Windows

The generated game functions, patches, vendored dependencies, and US big-endian
ROM described in [BUILDING.md](../BUILDING.md) must already be present.
Install Python 3, CMake, Ninja, JDK 17 or newer, Android SDK platform 34,
build-tools 35.0.0, platform-tools, and NDK r27 or newer.

```powershell
# Build a signed Release APK.
.\tools\build_quest.ps1

# Build, install, copy your ROM separately, and launch.
.\tools\build_quest.ps1 -Install -Launch

# Explicit tool locations and headset selection when needed.
.\tools\build_quest.ps1 -Install -Launch -Serial YOUR_QUEST_SERIAL `
  -Sdk C:\Android\Sdk -Ndk C:\Android\Sdk\ndk\27.2.12479018 `
  -JavaHome C:\Java\jdk-17 -Rom 'C:\Games\pokemonsnap.z64'
```

The output is `build-quest/Snap64RecompVR-quest-release.apk`, with a SHA-256
file beside it. The script verifies the signature before installing. Build
outputs are ignored by Git. `python tools/build_quest.py --help` lists the
underlying command's options.

The script reuses host `RSPRecomp.exe` and `file_to_c.exe` from `build-win`
when available. Otherwise it builds them in `build-quest/host`. It cross-compiles
the game and SDL with the NDK, compiles the HLSL shaders to SPIR-V on Windows,
compiles the Java SDL activity, packages the APK, and signs it. Gradle is not
required. Third-party license files are included in the APK assets.

The personal release signing key and password live in
`%USERPROFILE%\.android\snap64-quest`. Keep both files to sign compatible
updates. This is a locally signed Release build, not a store submission.
Installation uses `adb install -r` and preserves existing saves. The script
refuses an ambiguous device selection or a device that is not a Quest.

## On the headset

Enable developer mode and USB debugging, authorize this PC in the headset, and
wake both Touch controllers. Open **Snap64 Recomp VR** under **Unknown Sources**.
Quest may block launch while the controllers are unavailable.

Package: `org.snap64.quest`.

The app's writable files live at:

```text
/sdcard/Android/data/org.snap64.quest/files/
```

The build script copies `pokemonsnap.z64` there separately from the APK. It
never packages the ROM. Saves are in `saves/`, settings in `snapsettings.json`,
and logs in `snap64.log` and `snap64.prev.log`. Desktop native file dialogs are
unavailable; use ADB to copy a ROM, mods, or texture packs into this directory.

```powershell
adb -s YOUR_QUEST_SERIAL pull /sdcard/Android/data/org.snap64.quest/files/snap64.log
```

For performance diagnosis, create `quest-stats.enable` in that directory before
launching. Remove it to disable the extra renderer statistics on the next launch.
The VR log reports both headset submissions and distinct source game frames;
a high submitted FPS does not establish that the simulation or animation is
advancing smoothly.

## Rendering and validation

The port uses `XR_KHR_vulkan_enable2` for Vulkan instance and device creation,
retains the PC VR interaction code and accessory geometry, and submits separate
tracked eye images. Stereo replay uses RGBA8 consistently across the game
pipelines and eye targets. Quest defaults to 1.0 eye render scale and a 2x
original game pass; the latter is distinct from the stereo eye resolution.
The Android companion surface is fixed at 640x480 through its SurfaceHolder.
Its Vulkan surface is recreated when Android replaces the native window after
suspend. The desktop shader warm-up list is not replayed at startup on Android.
Stereo rendering reuses specialized material shaders, requests sustained CPU/GPU
performance through `XR_EXT_performance_settings` when available, and refreshes a
docked camera viewfinder at the source game's cadence. A held camera still
refreshes for each tracked frame. Rigid camera/vehicle/item meshes stay in GPU
buffers; pose and shutter motion are applied in the vertex shader. Skinned hand
vertices are uploaded once per tracked frame and reused for both eyes.

The VR tutorial panel mirrors dialogue separately from native camera focus
labels. Pokémon names remain in the original camera image and cannot create or
clear a tutorial panel. Tutorial replacements are published atomically, stay
steady through native blink updates, and clear when the confirmation wait ends.

Quest eye images use a raster transfer through compatible UNORM views of the
OpenXR sRGB images, preserving the game's encoded colors. On the tested Quest 3
this reduced the measured eye-transfer cost from about 9 ms to 1.7 ms per frame.
A raw image-copy fallback remains for runtimes without mutable-format support.

XR interpolation targets the runtime's application cadence, blending matched
animation poses between game frames. Fractional display intervals carry across
source frames (for example, the two/three-frame pattern for 30 Hz at 72 Hz).
A sample may finish past its source deadline; that overrun reduces the next
interval's budget. No additional sample starts after the deadline. The scheduling
source rate has a 30 Hz floor, so missed game draws cannot request progressively
more stale interpolated poses. Intentional slow motion still stretches its span.
Both headset submissions and distinct source game frames are logged; reaching
the headset's refresh rate depends on rendering time at the selected resolution.

The APK has been built, signed, installed, and launched on a Quest 3. OpenXR
has reached a focused session, loaded the user's ROM, and submitted stereo
frames. Initial device testing exposed startup, image-copy, render-format,
and performance problems. The interpolation fix recovered from 0.5–2 distinct
game frames/s to roughly 30 in the measured intro, and the user reported improved
animation. The requested 100% resolution is 1680x1760 per eye on the tested Quest
3. The user confirmed the intermittent sky flash was fixed. Course rendering
at that resolution remains below the 72 Hz target; after the transfer change,
the measured run delivered about 30 fresh game frames and 30 headset frames/s.
The user reported improvement after rigid-mesh caching. The subsequent
interpolation pacing update has been built and installed; deterministic tests
cover full 30-to-72 Hz cadence, deadline carryover, stalls, and refresh changes.
An active headset timing and visual check of that update remains pending.
This remains an experimental port: sustained
course performance, suspend/resume, photo evaluation, and long sessions require headset
validation. Do not interpret successful compilation or installation as that
validation. Quest 2, Quest Pro, and Quest 3S have not been tested.

Platform references: [Android SurfaceHolder](https://developer.android.com/reference/android/view/SurfaceHolder),
[Meta's Android manifest requirements](https://developers.meta.com/horizon/documentation/native/android/mobile-native-manifest/)
and the [Khronos OpenXR specification](https://registry.khronos.org/OpenXR/specs/1.0-khr/html/xrspec.html).
