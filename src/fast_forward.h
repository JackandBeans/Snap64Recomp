#ifndef SNAP_FAST_FORWARD_H
#define SNAP_FAST_FORWARD_H

#include <cstdint>

namespace snap {

// Fast forward and slow motion: while the keys table's fast_forward entry is
// held (Tab or the pad's right shoulder as shipped), every clock the game
// reads -- the retraces, the counter, osGetTime, the OS timers -- runs
// fast_forward_speed times faster than the wall clock; while slow_motion is
// held (Space, or the left stick pressed in), slow_motion_speed times slower
// (ultramodern set_speed_ratio). The game itself is unchanged either way and
// runs as on a faster or slower console: the same frames, the same scores,
// the same saves. Above 1x the renderer shows the latest frame at the
// display's rate with interpolation off; below it each game frame's span is
// stretched, so slow motion is smooth. The audio averages the extra samples
// down, or stretches them, so the sound keeps the picture's pace. Slow motion
// wins when both keys are down. SNAP_SPEED=<2..8> or <1/2..1/8> holds a whole
// run at that speed with no key, for a headless proof (tools/release_check.py,
// speed and slow).
//
// Called once per presented frame on the main thread (main.cpp update_gfx).
void fast_forward_tick();

// The speed in force, in thousandths of the console's (1000 at its own
// speed). Any thread.
uint32_t speed_permille();

} // namespace snap

#endif // SNAP_FAST_FORWARD_H
