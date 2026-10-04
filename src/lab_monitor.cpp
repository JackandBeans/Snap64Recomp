/**
 * @file lab_monitor.cpp
 * @brief The lab's monitor starts a move on a whole drawn frame when the
 *        frame rate is raised.
 *
 * The monitor in the top right of the lab's menus (the course's picture,
 * the album, Oak's items) eases to its place by a fifth of what is left on
 * every logic step (func_800E1CA0_8A74C0), and the lab runs two logic steps
 * for each frame it draws. An order to show or hide it that lands between
 * the two steps of a frame leaves that frame one step of the move and gives
 * every later frame two: it rises 16, 24, 16, 10 pixels a frame, where a
 * move that starts on a whole frame goes 29, 20, 12, 8. At the game's own
 * rate nobody can tell. Drawn between the frames, the monitor starts slow,
 * speeds up and slows again.
 *
 * So with the frame rate raised, the first step of a move takes the second
 * with it when it is the last step before a draw. For that move the monitor
 * is one logic step ahead of the cartridge, and it comes to rest in the
 * same place. Nothing in the game reads its position.
 */

#include <atomic>
#include <cstdint>
#include <cstdio>

#include "hle/rt64_snap_diag.h"
#include "recomp.h"
#include "settings.h"

extern "C" {
#include "funcs.h"
}

extern "C" std::atomic<uint32_t> snap_draw_serial;

namespace {
// D_801957EC_95B00C: nonzero while the monitor is to be shown.
constexpr uint32_t MonitorShownAddr = 0x801957ECu;
}  // namespace

extern "C" void func_800E1CA0_8A74C0(uint8_t* rdram, recomp_context* ctx) {
    static uint32_t lastDraw = 0;
    static uint32_t stepsSinceDraw = 0;
    static int32_t lastShown = -1;

    const uint32_t draw = snap_draw_serial.load(std::memory_order_relaxed);
    if (draw != lastDraw) {
        lastDraw = draw;
        stepsSinceDraw = 0;
    }

    const int32_t shown = (MEM_W(0, (gpr)(int32_t)MonitorShownAddr) != 0) ? 1 : 0;
    const bool turned = (lastShown >= 0) && (shown != lastShown);
    lastShown = shown;

    const recomp_context entry = *ctx;
    __real_func_800E1CA0_8A74C0(rdram, ctx);

    if (turned && (stepsSinceDraw == 1) && (snap::settings().fps_mode != 0)) {
        recomp_context again = entry;
        __real_func_800E1CA0_8A74C0(rdram, &again);
        if (snapdiag::statsEnabled()) {
            printf("[SNAP-2D] lab monitor: the first step of its way %s took the second with it (draw %u)\n",
                shown ? "down" : "up", draw);
            fflush(stdout);
        }
    }

    stepsSinceDraw++;
}
