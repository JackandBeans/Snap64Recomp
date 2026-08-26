/**
 * @file rect_census.cpp
 * @brief Attributes the game's screen-space rectangles to the code that emits them.
 *
 * Coverage is measured properly now -- 97.5% of the rectangles on screen in the
 * menus carry a name, and 18 to 21% during a ride -- so four out of five
 * rectangles in gameplay are still drawn at the rate the game draws at. That is
 * the number, but it does not say WHICH code draws them, and the answer decides
 * what is worth building next: the sprite library is reachable with a native
 * hook, while an emitter that builds its rectangles inline can only be reached
 * by replacing the function on the game side, which is a different and much
 * larger piece of work.
 *
 * So each candidate emitter is asked directly. Every one of them builds its
 * commands through gMainGfxPos, reading the write pointer and storing it back
 * as it goes, so the span a call wrote is exactly the bytes between the pointer
 * before it ran and the pointer after. Counting the texture-rectangle opcodes
 * in that span attributes them to that call and to nothing else.
 *
 * This measures and changes nothing. It is gated behind SNAP_STATS and the
 * counting is skipped entirely when that is off, so a release build pays for a
 * pointer read and a call.
 */
#include <cstdint>

#include "recomp.h"
#include "hle/rt64_snap_diag.h"

extern "C" {
#include "funcs.h"
}

namespace snap {
namespace {

constexpr uint32_t GMainGfxPos = 0x8004A890;

// G_TEXRECT and its flipped twin, in the top byte of a command word.
constexpr uint32_t TexRectOpcode = 0xE4;
constexpr uint32_t TexRectFlipOpcode = 0xE5;
constexpr uint32_t FillRectOpcode = 0xF6;

bool valid_ram_address(uint32_t address) {
    return ((address >> 29) == 4u) && ((address & 0x1FFFFFFFu) < 0x00800000u);
}

} // namespace
} // namespace snap

// Counts the rectangle commands in the display list a call just produced.
//
// Walked at command alignment. A texture rectangle is more than one word and
// the words after the first are coordinates, so a coordinate whose top byte
// happens to equal an opcode would be miscounted -- which is why this is used
// to compare emitters against each other rather than as an exact total.
static uint32_t snap_count_rects(uint8_t* rdram, uint32_t from, uint32_t to) {
    if (!snap::valid_ram_address(from) || !snap::valid_ram_address(to) || (to <= from)) {
        return 0;
    }

    // A single call that appears to have written more than the whole buffer is
    // not something to walk; the pointer was reset under us.
    if ((to - from) > 0x8000u) {
        return 0;
    }

    uint32_t count = 0;
    for (uint32_t cursor = from; (cursor + 8u) <= to; cursor += 8u) {
        const uint32_t word0 = static_cast<uint32_t>(MEM_W(0, (gpr)(int32_t)cursor));
        const uint32_t opcode = word0 >> 24;
        if ((opcode == snap::TexRectOpcode) || (opcode == snap::TexRectFlipOpcode) ||
            (opcode == snap::FillRectOpcode)) {
            count++;
        }
    }

    return count;
}

#define SNAP_CENSUS(name, counter)                                                       \
    extern "C" void name(uint8_t* rdram, recomp_context* ctx) {                          \
        if (!snapdiag::statsEnabled()) {                                           \
            __real_##name(rdram, ctx);                                                   \
            return;                                                                      \
        }                                                                                \
        const uint32_t before = static_cast<uint32_t>(MEM_W(0, (gpr)(int32_t)snap::GMainGfxPos)); \
        __real_##name(rdram, ctx);                                                       \
        const uint32_t after = static_cast<uint32_t>(MEM_W(0, (gpr)(int32_t)snap::GMainGfxPos)); \
        snapdiag::counter().fetch_add(snap_count_rects(rdram, before, after),      \
                                            std::memory_order_relaxed);                  \
    }

// The particle and effect system. This is the leading suspect for the untagged
// rectangles in gameplay: it is what puts the leaves out of the tall grass and
// the sand under Doduo on screen, and it builds every rectangle inline, inside
// three nested loops, calling nothing -- so there is no inner function a native
// hook could reach and no way to name individual effects without replacing the
// function on the game side.
SNAP_CENSUS(fx_draw, rectsFromEffectsCounter)

// Text. Emits its rectangles inline as well.
SNAP_CENSUS(Msg_DrawMessage, rectsFromTextCounter)

// The photo and score render path.
SNAP_CENSUS(func_8009E3D0, rectsFromPhotoCounter)

// The menu overlay's own copy of the sprite library. func_803719B0_845160 is
// the twin of renDrawSprite: it seeds a per-sprite cursor from gMainGfxPos and
// hands it down, so the span it writes is everything its sprites produced. Only
// the RESIDENT library is tagged, so if the interface draws through this one
// instead, none of what it draws carries a name.
SNAP_CENSUS(func_803719B0_845160, rectsFromWindowCounter)

// Background and depth clears, counted so they can be discounted: they fill the
// same place every frame and interpolating them would achieve nothing.
SNAP_CENSUS(renInitCamera, rectsFromCameraFillCounter)
SNAP_CENSUS(renInitCameraEx, rectsFromCameraFillCounter)
