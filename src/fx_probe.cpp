/**
 * @file fx_probe.cpp
 * @brief Watches the effect system's two fixed pools.
 *
 * Everything about the renderer's half of the effect sprites is now accounted
 * for. The rectangles arrive with sizes and primitive depths that agree with
 * each other and with the game's own camera, the depth decode is on the same
 * scale as geometry depth, the primitive colour defaults to opaque and the
 * combiner multiplies texel alpha by it, and replaying the palette and block
 * loads into texture memory by hand and running the shader's addressing over
 * the result reproduces the sprite exactly, texel for texel. Nothing there
 * removes them.
 *
 * What the recorded ride shows instead is a ceiling. The level asks for a
 * hundred particles and ten effects (func_beach_802C44D4), and by the second
 * half of the ride one effect alone is putting sixty-two particles on screen
 * every frame, with more alive off screen than on. Over the same stretch not a
 * single four-bit intensity rectangle is drawn -- not the forty-eight pixel
 * smoke puff, not the sparkle -- which is exactly what a full pool looks like
 * from the outside, because both allocators fail by returning nothing and
 * every caller drops the effect without a word.
 *
 * So these two hooks count the failures. fx_getEffect hands out Effect slots
 * and fx_createParticle hands out Particle slots; a null return from either is
 * an effect the player was supposed to see and did not.
 */

#include <cstdint>
#include <cstdio>

#include "recomp.h"

extern "C" {
#include "funcs.h"
}

namespace {

// Reported together so the two ceilings can be told apart: running out of
// effect slots loses a whole effect, running out of particle slots hollows out
// effects that were created successfully.
uint32_t g_effect_calls = 0;
uint32_t g_effect_empty = 0;
uint32_t g_particle_calls = 0;
uint32_t g_particle_empty = 0;

void report(const char* which) {
    printf("[SNAP-FXPOOL] %s empty -- effects %u/%u lost, particles %u/%u lost\n",
           which, g_effect_empty, g_effect_calls, g_particle_empty, g_particle_calls);
    fflush(stdout);
}

}  // namespace

extern "C" void fx_draw(uint8_t* rdram, recomp_context* ctx) {
    static uint32_t calls = 0;
    calls++;
    // A periodic line even when nothing is failing, so a clean run is
    // distinguishable from a run where the probe never fired.
    if ((calls % 900) == 0) {
        printf("[SNAP-FXPOOL] frame %u -- effects %u/%u lost, particles %u/%u lost\n",
               calls, g_effect_empty, g_effect_calls, g_particle_empty, g_particle_calls);
        fflush(stdout);
    }

    __real_fx_draw(rdram, ctx);
}

extern "C" void fx_createEffect(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t bank = (uint32_t)ctx->r4;
    const uint32_t script = (uint32_t)ctx->r5;

    __real_fx_createEffect(rdram, ctx);

    // Prints the identity of what was asked for, which the pool hooks below
    // cannot see. Sparse: this fires at animation-event rates.
    static uint32_t spawns = 0;
    spawns++;
    const bool failed = ((uint32_t)ctx->r2 == 0);
    if (failed || (spawns <= 20) || ((spawns % 50) == 0)) {
        printf("[SNAP-FXSPAWN] #%u bank %u script %u -> %08X%s\n",
               spawns, bank, script, (uint32_t)ctx->r2, failed ? "  REFUSED" : "");
        fflush(stdout);
    }
}

// Takes the next Effect off the free list, or nothing when the list is empty.
// fx_createEffect turns that into a null and every caller in the game drops the
// effect silently, so this is the only place the refusal is visible.
extern "C" void fx_getEffect(uint8_t* rdram, recomp_context* ctx) {
    __real_fx_getEffect(rdram, ctx);

    g_effect_calls++;
    if ((uint32_t)ctx->r2 == 0) {
        g_effect_empty++;
        if ((g_effect_empty <= 8) || ((g_effect_empty % 100) == 0)) {
            report("effect pool");
        }
    }
}

// The same for particles. Every particle in the game is born here, whether from
// an effect emitting, a script spawning a child, or a direct call.
extern "C" void fx_createParticle(uint8_t* rdram, recomp_context* ctx) {
    __real_fx_createParticle(rdram, ctx);

    g_particle_calls++;
    if ((uint32_t)ctx->r2 == 0) {
        g_particle_empty++;
        if ((g_particle_empty <= 8) || ((g_particle_empty % 500) == 0)) {
            report("particle pool");
        }
    }
}
