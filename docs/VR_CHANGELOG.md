# Snap64RecompVR releases

VR releases use their own version series, independent of the desktop port.
The VR version is defined by `SNAP_VR_VERSION` and
`SNAP_VR_VERSION_PRERELEASE` in `cmake/Version.cmake`. Desktop releases
continue to use `project(VERSION)` in `CMakeLists.txt` and `CHANGELOG.md`.

## 1.0

Initial VR release series:

- Windows OpenXR stereo rendering with tracked head and controller poses.
- A handheld camera with trigger photography, lens zoom and two-hand steadying.
- Physical item grabs and throws from the ZERO-ONE cockpit.
- An integrated Poké Flute button using the loaded game's icon, physical hand
  presses, and an inactive gray appearance until unlocked.
- Pointer menus and a VR settings panel; the original Options pages are hidden in VR.
- Corrected audio queue reporting to prevent growing sound delay.
- Headset-paced frame interpolation without desktop VSync or frame-dropping
  rules limiting VR; verified near 120 submitted FPS on a 120 Hz headset.
- Runtime logs report measured submitted FPS separately from the interpolation target.
- `Snap64RecompVR.exe` starts in VR by default; `--desktop` selects desktop mode.

The build remains experimental. See the VR guide in `docs/VR.md` for tested
behavior and outstanding headset checks.
