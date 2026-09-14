#ifndef SNAP_FAST_FORWARD_H
#define SNAP_FAST_FORWARD_H

#include <cstdint>

namespace snap {

// Fast forward: while the keys table's fast_forward entry is held (Tab or the
// pad's right shoulder as shipped), every clock the game reads -- the
// retraces, the counter, osGetTime, the OS timers -- runs fast_forward_speed
// times faster than the wall clock (ultramodern set_speed_multiplier), so the
// game itself is unchanged and runs as on a faster console: the same frames,
// the same scores, the same saves. The renderer shows the latest frame at the
// display's rate with interpolation off for the duration, and the audio
// averages the extra samples down so the sound keeps the picture's pace.
// SNAP_SPEED=<2..8> holds a whole run at that speed with no key, for a
// headless proof (tools/release_check.py, speed).
//
// Called once per presented frame on the main thread (main.cpp update_gfx).
void fast_forward_tick();

// The multiplier in force, 1 at the console's speed. Any thread.
uint32_t fast_forward_multiplier();

} // namespace snap

#endif // SNAP_FAST_FORWARD_H
