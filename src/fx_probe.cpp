/**
 * @file fx_probe.cpp
 * @brief Census of the effect system's live state, taken from game memory.
 *
 * Everything cheaper has been measured. The renderer reproduces a sprite
 * texel for texel when fed the ROM data; the rectangles arrive with sizes
 * and depths that agree with the game's camera; the pools never refuse an
 * allocation (873 effects and 1561 particles served in one ride, zero
 * lost). And yet the sleep symbols appear for one frame and vanish.
 *
 * One frame of life means creation, drawing, decoding and presentation all
 * worked once. Whatever ends it acts between frames, on the game's side of
 * the fence or on the renderer's. The two are distinguished by watching the
 * game's own lists: if the particles die or move or shrink after the first
 * frame, the fault is in their update; if they persist with sane fields
 * while the screen shows nothing, the fault is in what the renderer does
 * with them.
 *
 * fx_draw runs once per frame per effect camera, so it is the census point.
 * Bank 0 is the level's ambient effect (dozens of particles, known good) and
 * is only counted; every particle and effect in any other bank -- which is
 * where the sleep symbols and the tornado live -- is printed whole.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "recomp.h"

extern "C" {
#include "funcs.h"
}

namespace {

// The decompilation's symbol names are the virtual addresses.
constexpr uint32_t ParticleLists = 0x800BE1A8;  // Particle* D_800BE1A8[16]
constexpr uint32_t ActiveEffects = 0x800BE1EC;  // Effect* D_800BE1EC

uint32_t g_effect_calls = 0;
uint32_t g_effect_empty = 0;
uint32_t g_particle_calls = 0;
uint32_t g_particle_empty = 0;

float read_f32(uint8_t* rdram, uint32_t addr) {
    const uint32_t bits = (uint32_t)MEM_W(0, (gpr)(int32_t)addr);
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

}  // namespace

extern "C" void fx_draw(uint8_t* rdram, recomp_context* ctx) {
    static uint32_t frame = 0;
    frame++;

    // --- Effects: everything not in bank 0 is printed whole. ---
    bool interesting = false;
    {
        uint32_t fx = (uint32_t)MEM_W(0, (gpr)(int32_t)ActiveEffects);
        int guard = 0;
        while ((fx != 0) && (guard++ < 64)) {
            const uint32_t bank = MEM_BU(0x09, (gpr)(int32_t)fx);
            if ((bank & 7) != 0) {
                interesting = true;
                printf("[SNAP-FXEFF] f%u %08X bank %u kind %u tex %d fxLife %u pLife %u rate %.3f accum %.3f pos (%.1f,%.1f,%.1f) dobj %08X\n",
                       frame, fx, bank,
                       MEM_BU(0x08, (gpr)(int32_t)fx),
                       (int)(int16_t)MEM_HU(0x0A, (gpr)(int32_t)fx),
                       (uint32_t)MEM_HU(0x0E, (gpr)(int32_t)fx),
                       (uint32_t)MEM_HU(0x0C, (gpr)(int32_t)fx),
                       read_f32(rdram, fx + 0x40), read_f32(rdram, fx + 0x44),
                       read_f32(rdram, fx + 0x14), read_f32(rdram, fx + 0x18), read_f32(rdram, fx + 0x1C),
                       (uint32_t)MEM_W(0x48, (gpr)(int32_t)fx));
            }
            fx = (uint32_t)MEM_W(0x00, (gpr)(int32_t)fx);
        }
    }

    // --- Particles: bank 0 is counted, the rest are printed whole. ---
    uint32_t bankZero = 0;
    for (int list = 0; list < 16; list++) {
        uint32_t p = (uint32_t)MEM_W(list * 4, (gpr)(int32_t)ParticleLists);
        int guard = 0;
        while ((p != 0) && (guard++ < 256)) {
            const uint32_t bank = MEM_BU(0x08, (gpr)(int32_t)p);
            if ((bank & 7) == 0) {
                bankZero++;
            }
            else {
                interesting = true;
                printf("[SNAP-FXPART] f%u %08X bank %u tex %u frame %u life %u size %.2f pos (%.1f,%.1f,%.1f) prim %02X%02X%02X%02X env.a %02X flags %04X\n",
                       frame, p, bank,
                       MEM_BU(0x0A, (gpr)(int32_t)p),
                       MEM_BU(0x0B, (gpr)(int32_t)p),
                       (uint32_t)MEM_HU(0x1E, (gpr)(int32_t)p),
                       read_f32(rdram, p + 0x40),
                       read_f32(rdram, p + 0x20), read_f32(rdram, p + 0x24), read_f32(rdram, p + 0x28),
                       MEM_BU(0x48, (gpr)(int32_t)p), MEM_BU(0x49, (gpr)(int32_t)p),
                       MEM_BU(0x4A, (gpr)(int32_t)p), MEM_BU(0x4B, (gpr)(int32_t)p),
                       MEM_BU(0x53, (gpr)(int32_t)p),
                       (uint32_t)MEM_HU(0x06, (gpr)(int32_t)p));
            }
            p = (uint32_t)MEM_W(0x00, (gpr)(int32_t)p);
        }
    }

    if (interesting || ((frame % 900) == 0)) {
        printf("[SNAP-FXPOOL] f%u bank0 %u -- effects %u/%u lost, particles %u/%u lost\n",
               frame, bankZero, g_effect_empty, g_effect_calls, g_particle_empty, g_particle_calls);
        fflush(stdout);
    }

    __real_fx_draw(rdram, ctx);
}

extern "C" void fx_createEffect(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t bank = (uint32_t)ctx->r4;
    const uint32_t script = (uint32_t)ctx->r5;

    __real_fx_createEffect(rdram, ctx);

    // Every non-bank-0 creation prints: these are the event-driven effects
    // (sleep symbols, tornado) and they are rare. Bank 0 stays sparse.
    static uint32_t spawns = 0;
    spawns++;
    const bool failed = ((uint32_t)ctx->r2 == 0);
    if (failed || ((bank & 7) != 0) || (spawns <= 20) || ((spawns % 50) == 0)) {
        printf("[SNAP-FXSPAWN] #%u bank %u script %u -> %08X%s\n",
               spawns, bank, script, (uint32_t)ctx->r2, failed ? "  REFUSED" : "");
        fflush(stdout);
    }
}

// The allocation choke points stay counted; both fail silently.
extern "C" void fx_getEffect(uint8_t* rdram, recomp_context* ctx) {
    __real_fx_getEffect(rdram, ctx);
    g_effect_calls++;
    if ((uint32_t)ctx->r2 == 0) {
        g_effect_empty++;
    }
}

extern "C" void fx_createParticle(uint8_t* rdram, recomp_context* ctx) {
    __real_fx_createParticle(rdram, ctx);
    g_particle_calls++;
    if ((uint32_t)ctx->r2 == 0) {
        g_particle_empty++;
    }
}

// Materials are the other way the game animates a picture: a mesh whose
// texture is swapped per frame from a flipbook of images, driven by the
// material's lodLevel. Snorlax's sleep symbols are one of these -- a model
// part, not a particle -- which is why the particle census above never saw
// them. Only Pokemon models carry materials, so the volume is small enough
// to log each build whole, after the real function has chosen the frame.
extern "C" void renLoadTextures(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t dobj = (uint32_t)ctx->r4;

    __real_renLoadTextures(rdram, ctx);

    const uint32_t mobjHead = (uint32_t)MEM_W(0x80, (gpr)(int32_t)dobj);
    if (mobjHead == 0) {
        return;
    }

    static uint32_t builds = 0;
    builds++;

    char line[512];
    int len = snprintf(line, sizeof(line), "[SNAP-MAT] #%u dobj %08X", builds, dobj);
    uint32_t mobj = mobjHead;
    int guard = 0;
    while ((mobj != 0) && (guard++ < 6) && (len < (int)sizeof(line) - 96)) {
        float lod;
        const uint32_t lodBits = (uint32_t)MEM_W(0x84, (gpr)(int32_t)mobj);
        std::memcpy(&lod, &lodBits, sizeof(lod));
        const uint32_t imageIndex = MEM_HU(0x80, (gpr)(int32_t)mobj);
        const uint32_t images = (uint32_t)MEM_W(0x0C, (gpr)(int32_t)mobj);
        const uint32_t image = (images != 0) ? (uint32_t)MEM_W(imageIndex * 4, (gpr)(int32_t)images) : 0;
        len += snprintf(line + len, sizeof(line) - len,
                        " | fl %04X lod %.2f idx %u img %08X a %02X",
                        (uint32_t)MEM_HU(0x38, (gpr)(int32_t)mobj), lod, imageIndex, image,
                        MEM_BU(0x5B, (gpr)(int32_t)mobj));
        mobj = (uint32_t)MEM_W(0x00, (gpr)(int32_t)mobj);
    }
    printf("%s\n", line);
    if ((builds % 64) == 0) {
        fflush(stdout);
    }
}
