/**
 * @file fast_forward.cpp
 * @brief The fast-forward key: the game run faster than the console ran it.
 *
 * Nothing about the game changes. The runtime's clocks are told to run
 * `multiplier` times faster (ultramodern::set_speed_multiplier), so the game
 * gets its retraces, its audio interrupts, its counter and its timers that
 * much sooner in wall-clock time and steps through exactly the frames it
 * would have stepped through anyway; a replay run at 3x scores the same
 * photos with the same numbers in a third of the time. What the player sees
 * and hears follows: the renderer presents the latest frame at the display's
 * rate (rt64_workload_queue.cpp, interpolation off while the multiplier is
 * above 1) and the audio path averages every `multiplier` stereo pairs into
 * one (audio.cpp), quicker and higher the way a tape is in fast forward.
 */
#include "fast_forward.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>

#include "ultramodern/ultramodern.hpp"
#include "hle/rt64_snap_diag.h"

#include "audio.h"
#include "input.h"
#include "settings.h"

namespace snap {

namespace {

// What is in force; the main thread's, published for any other reader.
uint32_t s_multiplier = 1;
std::atomic<uint32_t> s_published{1};

// SNAP_SPEED=<2..8> holds the whole run at that speed, key or no key: the
// headless proof (tools/release_check.py, speed) and a way to run a replay
// in a fraction of its time. Not a setting.
uint32_t fixed_multiplier() {
    static const uint32_t fixed = [] {
        const char* e = std::getenv("SNAP_SPEED");
        const long n = (e != nullptr) ? std::strtol(e, nullptr, 10) : 0;
        return ((n >= 2) && (n <= 8)) ? uint32_t(n) : 0u;
    }();
    return fixed;
}

} // namespace

void fast_forward_tick() {
    uint32_t wanted = 1;
    const uint32_t fixed = fixed_multiplier();
    if (fixed != 0) {
        wanted = fixed;
    }
    else if (input_fast_forward_held()) {
        int speed;
        {
            std::lock_guard<std::mutex> lock(settings_mutex());
            speed = settings().fast_forward_speed;
        }
        wanted = (speed >= 2) ? uint32_t(std::min(speed, 4)) : 1u;
    }
    if (wanted == s_multiplier) {
        return;
    }
    s_multiplier = wanted;
    // The clocks first, so the renderer and the audio never see a speed the
    // game is not yet running at; each reads its own copy on its own thread.
    ultramodern::set_speed_multiplier(wanted);
    snapdiag::speedMultiplier().store(wanted, std::memory_order_relaxed);
    audio_set_speed(wanted);
    s_published.store(wanted, std::memory_order_relaxed);
    if (wanted == 1) {
        printf("[SNAP] fast forward: off\n");
    }
    else if (fixed != 0) {
        printf("[SNAP] fast forward: %ux for the whole run (SNAP_SPEED)\n", wanted);
    }
    else {
        printf("[SNAP] fast forward: %ux while the key is held\n", wanted);
    }
    fflush(stdout);
}

uint32_t fast_forward_multiplier() {
    return s_published.load(std::memory_order_relaxed);
}

} // namespace snap
