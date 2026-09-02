/**
 * @file intro_probe.cpp
 * @brief Marks the two ends of a course intro's hand-off glide in draw
 *        counts, so the toggle in patches/src/beach_intro_patch.c and
 *        river_intro_patch.c can be measured in frames the game drew rather
 *        than inferred from what the display showed.
 *
 * PlayerModel_SetAnimation is the intro coroutine's first call after it reads
 * the toggle byte; func_beach_802C5214 and func_802E2194_6C9F74 are the two
 * courses' hand-offs, the calls that delete the model. The draw serial is
 * gtlDraw's count. Prints only under SNAP_STATS; reads two registers and
 * changes nothing.
 */

#include <atomic>
#include <cstdint>
#include <cstdio>

#include "hle/rt64_snap_diag.h"
#include "recomp.h"

extern "C" {
#include "funcs.h"
}

extern "C" std::atomic<uint32_t> snap_draw_serial;

namespace {
void mark(const char* what, uint32_t a0) {
    if (snapdiag::statsEnabled()) {
        printf("[SNAP-INTRO] %s at draw %u (a0 %08X)\n", what, snap_draw_serial.load(std::memory_order_relaxed), a0);
        fflush(stdout);
    }
}
}  // namespace

extern "C" void PlayerModel_SetAnimation(uint8_t* rdram, recomp_context* ctx) {
    mark("PlayerModel_SetAnimation", static_cast<uint32_t>(ctx->r4));
    __real_PlayerModel_SetAnimation(rdram, ctx);
}

extern "C" void func_beach_802C5214(uint8_t* rdram, recomp_context* ctx) {
    mark("beach hand-off", 0);
    __real_func_beach_802C5214(rdram, ctx);
}

extern "C" void func_802E2194_6C9F74(uint8_t* rdram, recomp_context* ctx) {
    mark("river hand-off", 0);
    __real_func_802E2194_6C9F74(rdram, ctx);
}
