# VR item controls and tutorial replacement report

## ZERO-ONE flute button

The left dashboard has a gold **POKE FLUTE** button within seated reach. With an empty hand, reach to the cap and press down, or pull trigger near it. Withdraw before pressing again. It is gray and marked **LOCKED** until the original flute unlock is earned. During playback it turns green and shows seconds remaining.

Each press plays native flute music and Pokemon reaction commands for **10 wall-clock seconds**. Pressing again during playback changes the tune and restarts the timer. Short tunes loop without switching melodies. Throwing bait or a Pester Ball does not interrupt the song. Pause, headset focus loss, and cutscenes cancel playback; scene changes discard pending requests. Native course music resumes through the existing music process.

A/X now only advances in-course dialogue. No saved progression is changed.

## Exact tutorial replacements

Five pages of Oak's item-unlock explanations are replaced in VR, including repeated explanations. The original wording is deliberately not reproduced.

| Tutorial | Page (zero-based index) | Replaced instruction | Replacement |
| --- | --- | --- | --- |
| Bait / Pokemon Food | 3 (2) | Legacy button throw and camera-mode restriction | A |
| Bait / Pokemon Food | 5 (4) | Head-direction-based throwing distance guidance | B |
| Pester Ball | 2 (1) | Legacy button throw and camera-mode restriction | C |
| Poke Flute | 3 (2) | Legacy button playback instruction | D |
| Poke Flute | 4 (3) | Legacy button tune-changing instruction | E |

A ? bait pickup and throw:

```text
Grip bait from the rear
right dispenser. Swing your
hand; release grip to throw.
```

B ? bait throwing distance:

```text
For more distance, swing
faster and release upward.
Try this next:
```

C ? Pester Ball pickup and throw:

```text
Grip a Pester Ball from the
front right dispenser. Swing
and release grip to throw.
```

D ? flute playback:

```text
Press the POKE FLUTE button
on the ZERO-ONE to play
music for 10 seconds.
```

E ? flute tune change:

```text
Press the ZERO-ONE button
again to change the tune
and restart the 10 seconds.
```

Both dispensers are on the right of the ZERO-ONE: Pester Balls in front, bait behind. Either free hand can pick up and throw; the camera may stay in the other hand. The original item-purpose explanations and Pokemon interaction hints remain. The port's own `docs/VR.md` controls also replace the A/X flute binding with the cart-button instructions.

## No original dialogue in the replacement code

The hook identifies the matching USA runtime tables numerically in overlay 114:

| Item | Section offset | Replaced indices |
| --- | --- | --- |
| Bait | `0xB4558` | `2`, `4` |
| Pester Ball | `0xB4578` | `1` |
| Flute | `0xB4588` | `2`, `3` |

`func_800E4578_8A9D98` temporarily substitutes these array entries with newly written prose held on the guest stack, then restores the original entries. No original dialogue string, substring, or byte signature is embedded or matched. Original ROM text remains in the user's ROM. Non-VR mode passes through untouched. Hooks are regenerated with `tools/hook_funcs.py`; generated functions are not manually edited.

## Validation

Windows Release build and automated interaction tests pass. Tests cover both hands, unlocks, tracking loss/recovery, re-arming, nearby-trigger activation, cinematic suppression, dialogue advancement, timer boundaries/restart/cancellation, unique replacement IDs, and reserved text-buffer bounds.

The scripted VR preview logged the native flute stop **10.000 seconds** after its second press. The cart button and all five replacement pages were visually checked in the game's VR preview, including preserved item artwork and the following interaction hint. Actual headset reach, audible output, and comfort have not been verified in this pass.

Preview diagnostics: `SNAP_VR_FLUTE_TEST=1` simulates two button presses with a temporary host-only unlock. `SNAP_VR_TUTORIAL_TEST=bait`, `pester`, or `flute` exercises the selected dialogue at the first lab conversation and captures its display. These require `--vr-preview`; they do not write progression flags.
