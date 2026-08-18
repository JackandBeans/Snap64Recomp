/**
 * @file patch_shims.cpp
 * @brief Name bridges between recompiled patches and the recompiled game.
 *
 * A function whose name collides with the C standard library is emitted with a
 * _recomp suffix when the game is recompiled, so the host compiler cannot
 * mistake it for its own. A patch calling that function is recompiled
 * separately, resolves it through the reference symbols, and emits the plain
 * name, which then matches nothing at link time.
 *
 * Forward the plain name to the game's. Each of these exists only because the
 * two recompiler runs disagree about what to call the same function.
 */

#include <cstdint>

#include "recomp.h"

extern "C" {
#include "funcs.h"

void __sinf(uint8_t* rdram, recomp_context* ctx) {
    __sinf_recomp(rdram, ctx);
}

void __cosf(uint8_t* rdram, recomp_context* ctx) {
    __cosf_recomp(rdram, ctx);
}
}
