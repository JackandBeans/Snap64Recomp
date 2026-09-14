/**
 * @file fast_forward.cpp
 * @brief The fast-forward and slow-motion keys: the game run at another
 * speed than the console ran it.
 *
 * Nothing about the game changes. The runtime's clocks are told to run at a
 * ratio of the wall clock (ultramodern::set_speed_ratio), so the game gets
 * its retraces, its audio interrupts, its counter and its timers that much
 * sooner or later in wall-clock time and steps through exactly the frames it
 * would have stepped through anyway; a replay run at 3x or at 1/2 scores the
 * same photos with the same numbers. What the player sees and hears follows:
 * above 1x the renderer presents the latest frame at the display's rate
 * (rt64_workload_queue.cpp, interpolation off), below it each game frame's
 * span is stretched so the blend runs through it at the slower pace; the
 * audio path averages every N stereo pairs into one, or stretches each into
 * N (audio.cpp), quicker and higher or slower and lower the way a tape is.
 */
#include "fast_forward.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "ultramodern/ultramodern.hpp"
#include "hle/rt64_snap_diag.h"

#include "audio.h"
#include "input.h"
#include "settings.h"

namespace snap {

namespace {

struct Ratio {
    uint32_t num;
    uint32_t den;
};

// What is in force; the main thread's, published for any other reader.
Ratio s_ratio{1, 1};
std::atomic<uint32_t> s_permille{1000};

// SNAP_SPEED=<2..8> or <1/2..1/8> holds the whole run at that speed, key or
// no key: the headless proof (tools/release_check.py, speed and slow) and a
// way to run a replay in a fraction of its time. Not a setting.
Ratio fixed_ratio() {
    static const Ratio fixed = [] {
        Ratio r{1, 1};
        const char* e = std::getenv("SNAP_SPEED");
        if (e == nullptr) {
            return r;
        }
        const char* slash = std::strchr(e, '/');
        if (slash != nullptr) {
            const long num = std::strtol(e, nullptr, 10);
            const long den = std::strtol(slash + 1, nullptr, 10);
            if ((num == 1) && (den >= 2) && (den <= 8)) {
                r.den = uint32_t(den);
            }
        } else {
            const long n = std::strtol(e, nullptr, 10);
            if ((n >= 2) && (n <= 8)) {
                r.num = uint32_t(n);
            }
        }
        return r;
    }();
    return fixed;
}

} // namespace

void fast_forward_tick() {
    Ratio wanted{1, 1};
    const Ratio fixed = fixed_ratio();
    const bool is_fixed = (fixed.num != 1) || (fixed.den != 1);
    if (is_fixed) {
        wanted = fixed;
    }
    else {
        const bool slow_held = input_slow_motion_held();
        const bool fast_held = input_fast_forward_held();
        if (slow_held || fast_held) {
            int fast_speed = 1;
            int slow_speed = 1;
            {
                std::lock_guard<std::mutex> lock(settings_mutex());
                fast_speed = settings().fast_forward_speed;
                slow_speed = settings().slow_motion_speed;
            }
            // Slow motion wins when both are down: it is the deliberate
            // one, a shot being lined up.
            if (slow_held && (slow_speed >= 2)) {
                wanted.den = uint32_t(std::min(slow_speed, 8));
            }
            else if (fast_held && (fast_speed >= 2)) {
                wanted.num = uint32_t(std::min(fast_speed, 8));
            }
        }
    }
    if ((wanted.num == s_ratio.num) && (wanted.den == s_ratio.den)) {
        return;
    }
    const Ratio before = s_ratio;
    s_ratio = wanted;
    // The clocks first, so the renderer and the audio never see a speed the
    // game is not yet running at; each reads its own copy on its own thread.
    ultramodern::set_speed_ratio(wanted.num, wanted.den);
    const uint32_t permille = (wanted.num * 1000u) / wanted.den;
    snapdiag::speedPermille().store(permille, std::memory_order_relaxed);
    audio_set_speed(wanted.num, wanted.den);
    s_permille.store(permille, std::memory_order_relaxed);
    if ((wanted.num == 1) && (wanted.den == 1)) {
        printf("[SNAP] %s: off\n", (before.den > 1) ? "slow motion" : "fast forward");
    }
    else if (wanted.den > 1) {
        printf("[SNAP] slow motion: %ux slower %s\n", wanted.den, is_fixed ? "for the whole run (SNAP_SPEED)" : "while the key is held");
    }
    else {
        printf("[SNAP] fast forward: %ux %s\n", wanted.num, is_fixed ? "for the whole run (SNAP_SPEED)" : "while the key is held");
    }
    fflush(stdout);
}

uint32_t speed_permille() {
    return s_permille.load(std::memory_order_relaxed);
}

} // namespace snap
