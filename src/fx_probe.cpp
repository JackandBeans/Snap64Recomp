/**
 * @file fx_probe.cpp
 * @brief Says whether the effect system runs at all.
 *
 * Every effect in the game -- Snorlax's sleep symbols, Meowth's tornado, the
 * splashes and sparkles -- is drawn by fx_draw, and none of them reach the
 * renderer: a probe on the texture rectangle path printed nothing across a
 * whole ride. fx_draw is not part of the main scene pass; the effect system
 * creates its own camera object and registers fx_draw as that camera's render
 * function (effect.c:95), so the whole family disappears together if that one
 * camera never renders.
 *
 * Two hooks split the remaining space. If fx_draw is never called, the fx
 * camera is not rendering and the fault is in the camera pass. If it is
 * called but nothing reaches the renderer, the fault is inside fx_draw --
 * notably one of the few functions the decompilation still carries as
 * assembly -- or the particles it would draw never existed, which is what the
 * spawn hook reports.
 */

#include <cstdint>
#include <cstdio>

#include "recomp.h"

extern "C" {
#include "funcs.h"
}

extern "C" void fx_draw(uint8_t* rdram, recomp_context* ctx) {
    static uint32_t calls = 0;
    calls++;
    if ((calls == 1) || ((calls % 300) == 0)) {
        printf("[SNAP-FXDRAW] call #%u camObj %08X\n", calls, (uint32_t)ctx->r4);
        fflush(stdout);
    }

    __real_fx_draw(rdram, ctx);
}

extern "C" void fx_createEffect(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t bank = (uint32_t)ctx->r4;
    const uint32_t script = (uint32_t)ctx->r5;

    __real_fx_createEffect(rdram, ctx);

    static uint32_t spawns = 0;
    spawns++;
    if ((spawns <= 20) || ((spawns % 50) == 0)) {
        printf("[SNAP-FXSPAWN] #%u bank %u script %u -> %08X\n",
               spawns, bank, script, (uint32_t)ctx->r2);
        fflush(stdout);
    }
}
