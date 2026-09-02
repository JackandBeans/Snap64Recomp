/**
 * @file score_probe.cpp
 * @brief Reads the photo-score buffers back after the game has scored a
 *        photo, so the copy-mode low bit can be checked in the game's own
 *        data rather than argued from the shader.
 *
 * score.c scores a photo by rendering the scene twice per Pokemon and
 * copying the depth image into colour-format buffers with a G_CYC_COPY rect
 * (score_GfxCopyZBuffer). The third clause of its per-pixel test compares
 * that snapshot against the live depth buffer for equality. On hardware a
 * copy-mode rect writes the fetched texel's sixteen bits verbatim; the port's
 * renderer used to synthesise the low bit from coverage, which made every
 * snapshot pixel odd against an always-even depth buffer, and the clause
 * could never fail.
 *
 * func_800A007C runs both renders and the counting loops; when it returns
 * every buffer is final and the score_* globals still point at them. This
 * wrapper reads them (64x48 u16 each) and prints, per Pokemon:
 *   - odd:   snapshot pixels with bit 0 set (before the fix: all 3072)
 *   - U:     pixels nothing overdrew after the snapshot (same value, low
 *            two bits ignored)
 *   - eq:    pixels in U where snapshot == live (after the fix: all of U)
 *   - or1:   pixels in U where snapshot == (live | 1) (before: all of U)
 *   - the game's own unobstructed count recomputed from the buffers, next
 *     to score_PixelCountUnobstructed, which must match exactly or the probe
 *     ran at the wrong moment.
 * Prints only under SNAP_STATS. Reads game memory; writes nothing.
 * Addresses from build/pokemonsnap.map.
 *
 * Measured on a recorded run through five scored photos (eval.inputs), same
 * replay, two builds. Renderer before the copy-mode fix: odd 3072, eq 0,
 * or1 == U on every Pokemon of every photo. After: odd 0, eq == U, or1 0.
 * The recomputed unobstructed count matched the game's own in all twenty
 * rows of both runs, and was the same before and after for these photos --
 * the clause the fix restores only bites on exact depth ties, and these
 * shots had none.
 */

#include <cstdint>
#include <cstdio>

#include "hle/rt64_snap_diag.h"
#include "recomp.h"

extern "C" {
#include "funcs.h"
}

namespace {

constexpr uint32_t ScoreCurrentZBuffer          = 0x800BDFB4u;  // u16* (one buffer)
constexpr uint32_t ScorePokemonZBuffer          = 0x800BDFB8u;  // u16*[12]
constexpr uint32_t ScoreZBufferBefore           = 0x800BDFE8u;  // u16*[12]
constexpr uint32_t ScorePokemonCount            = 0x800BE01Au;  // u16
constexpr uint32_t ScorePixelCountUnobstructed  = 0x800BE0B0u;  // s32[12]
constexpr uint32_t ScorePixels                  = 64 * 48;
constexpr uint32_t ScoreBufferBytes             = ScorePixels * 2;

bool in_ram(uint32_t addr, uint32_t bytes) {
    return addr >= 0x80000000u && (addr + bytes) <= 0x80800000u && (addr + bytes) >= addr;
}

uint32_t rd32(uint8_t* rdram, uint32_t addr) {
    return static_cast<uint32_t>(MEM_W(0x0, (gpr)(int32_t)addr));
}

uint16_t rd16(uint8_t* rdram, uint32_t addr) {
    return static_cast<uint16_t>(MEM_HU(0x0, (gpr)(int32_t)addr));
}

}  // namespace

extern "C" void func_800A007C(uint8_t* rdram, recomp_context* ctx) {
    __real_func_800A007C(rdram, ctx);
    if (!snapdiag::statsEnabled()) {
        return;
    }

    const uint32_t count = rd16(rdram, ScorePokemonCount);
    const uint32_t currentZ = rd32(rdram, ScoreCurrentZBuffer);
    if (count == 0 || count > 12 || !in_ram(currentZ, ScoreBufferBytes)) {
        printf("[SNAP-SCORE] count %u currentZ %08X: nothing to read\n", count, currentZ);
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        const uint32_t before = rd32(rdram, ScoreZBufferBefore + 4 * i);
        const uint32_t pokeZ = rd32(rdram, ScorePokemonZBuffer + 4 * i);
        if (!in_ram(before, ScoreBufferBytes) || !in_ram(pokeZ, ScoreBufferBytes)) {
            printf("[SNAP-SCORE] pokemon %u: buffers %08X %08X out of range\n", i, before, pokeZ);
            continue;
        }
        uint32_t odd = 0, u = 0, eq = 0, or1 = 0, unobstructed = 0;
        for (uint32_t px = 0; px < ScorePixels; px++) {
            const uint16_t b = rd16(rdram, before + 2 * px);
            const uint16_t z = rd16(rdram, currentZ + 2 * px);
            const uint16_t p = rd16(rdram, pokeZ + 2 * px);
            if (b & 1) {
                odd++;
            }
            if ((b & ~3u) == (z & ~3u)) {
                u++;
                if (b == z) {
                    eq++;
                }
                if (b == (z | 1)) {
                    or1++;
                }
            }
            // score.c's own clause, recomputed: the Pokemon's depth is present,
            // it is what the scene ended up with, and it was not there before.
            if (p != 0xFFFC && p == z && b != z) {
                unobstructed++;
            }
        }
        const int32_t gameCount = static_cast<int32_t>(rd32(rdram, ScorePixelCountUnobstructed + 4 * i));
        printf("[SNAP-SCORE] pokemon %u: odd %u  U %u  eq %u  or1 %u  unobstructed recomputed %u game %d%s\n",
               i, odd, u, eq, or1, unobstructed, gameCount,
               (static_cast<int32_t>(unobstructed) == gameCount) ? "" : "  MISMATCH");
    }
    fflush(stdout);
}
