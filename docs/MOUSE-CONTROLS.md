# Mouse controls

Mouse aiming uses SDL relative motion while a course is running and the window
has focus. Left mouse maps to A; hold right mouse for Z/the viewfinder, then
click left to take a photo. The stage intro still locks the camera temporarily.

| Preference | Binding | Default |
| --- | --- | --- |
| Mouse aim enabled | M | `mouse_enabled = true` |
| Invert vertical aim | Y | `invert_y = false` |
| Sensitivity | Numpad minus / plus | `mouse_sensitivity = 0.06` |

Each sensitivity step multiplies by 0.8 or 1.25, within 0.001–1.0. All three
preferences save through the existing debounced settings writer and restore
on launch. Existing settings override defaults. Current values appear in the
window title. Inversion applies to combined mouse, keyboard and controller
aim during gameplay; menu navigation is unchanged.

SDL event collection and relative-mode changes run on the main thread. Pending
motion and held/latched button presses are protected by a mutex until a
controller reading consumes them. A complete click between readings remains
latched for one reading, and each motion delta is consumed once. The existing
circular N64 stick limit remains in effect.

The game thread publishes course/pause state using app_level residency and the
retail `IsPaused` byte at `0x80382D20`. Capture releases and input clears on
focus loss, pause, leaving the course, or disabling mouse aim. Replays use
their recorded final input and never capture the mouse.

## Validation

On Windows, build the game and the optional production-input harness:

```powershell
cmake --build build-win --config Release --target Snap64Recomp SnapControlsTest
& .\build-win\Release\SnapControlsTest.exe
& .\build-win\Release\SnapControlsTest.exe --replay
```

Run from a writable directory; replay mode writes `controls-test.inputs` there.
The harness briefly opens an SDL window and compiles production input code,
with unrelated services and the settings store stubbed. It covers accumulated
motion, quick clicks, held/released viewfinder, inversion, stick bounds, focus,
pause, overlay transitions, disabling mouse aim, replay bypass, and replay EOF.
It passed 27 live checks and 8 replay checks on Windows/VS2022.

Separate real-application checks verified fresh defaults, numpad sensitivity,
automatic saving of M alone, and all three preferences across three launches.
Mouse controls were also playtested by the requester. Linux was not tested.
