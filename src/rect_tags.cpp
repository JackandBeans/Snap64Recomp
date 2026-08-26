/**
 * @file rect_tags.cpp
 * @brief Names the 2D elements Pokemon Snap draws, so the renderer can move them.
 *
 * The world interpolates because every matrix in it is tagged: the game says
 * which object each transform belongs to, and RT64 finds that same object in
 * the previous frame and draws it between the two positions. Sprites had no
 * such thing, and they are drawn a completely different way -- as texture
 * rectangles, which carry no matrix and no vertices, only the final screen
 * coordinates. So Doduo's sand, the leaves out of the tall grass, the HUD and
 * every menu ran at the rate the game draws at while everything behind them
 * ran at the display's.
 *
 * Guessing which rectangle was which from the rectangles themselves was tried,
 * and it is not solvable from that side: the laboratory background is a stack
 * of identical full-width strips, and any measure that pairs one strip with
 * "the nearest similar rectangle" pairs it with its neighbour. The result was
 * a background torn into sliding bands.
 *
 * The identity has to come from the game, and the game has it. Every sprite is
 * owned by an SObj whose address is stable for as long as it exists and is
 * different from every other one -- the same property render_patch.c already
 * relies on for object matrices. This writes that address into the display
 * list, in band, immediately before the sprite draws, using the extension RT64
 * already speaks.
 *
 * The sprite library is a good place to stand: renDrawSprite hands each SObj's
 * Sprite to spX2Draw after seeding the sprite's own display-list cursor from
 * gMainGfxPos, and spX2Draw is the only caller of drawbitmap, which is the only
 * thing in the resident code that emits a texture rectangle for a sprite. One
 * hook covers all of them.
 */
#include <cstdint>

#include "recomp.h"

extern "C" {
#include "funcs.h"
}

namespace snap {
namespace {

// Two commands, four words, the same shape the camera's matrix group uses.
constexpr uint32_t Param(uint32_t value, uint32_t width, uint32_t shift) {
    return (value & ((1u << width) - 1u)) << shift;
}

constexpr uint32_t HookOpcode = 0xE0;
constexpr uint32_t HookMagicNumber = 0x525464;
constexpr uint32_t HookOpEnable = 0x1;
constexpr uint32_t ExtendedOpcode = 0x64;
constexpr uint32_t RectGroupV1 = 0x000035;

constexpr uint32_t EnableWord0 = Param(HookOpcode, 8, 24) | Param(HookMagicNumber, 24, 0);
constexpr uint32_t EnableWord1 = Param(HookOpEnable, 4, 28) | Param(ExtendedOpcode, 8, 0);
constexpr uint32_t RectGroupWord0 = Param(ExtendedOpcode, 8, 24) | Param(RectGroupV1, 24, 0);

constexpr uint32_t TagBytes = 16;

// The sprite library's own fields, from the shipped code: spX2Draw reads its
// display-list cursor from Sprite+0x3C and returns without writing anything
// when the hidden bit is set, and renDrawSprite reaches the Sprite as SObj+0x10.
constexpr uint32_t SpriteCursorOffset = 0x3C;
constexpr uint32_t SpriteFlagsOffset = 0x14;
constexpr uint32_t SpriteHiddenFlag = 0x4;
constexpr uint32_t SObjFromSprite = 0x10;

// The game's display-list bookkeeping, as dl_budget.cpp reads it.
constexpr uint32_t GtlDLBuffers = 0x8004A850;
constexpr uint32_t GMainGfxPos = 0x8004A890;
constexpr uint32_t GtlContextId = 0x8004A910;
constexpr uint32_t DLBufferSize = 8;
constexpr uint32_t BufferKinds = 4;

// The same 8MB the game sees, tested without masking, because the MEM_ macros
// do not mask either -- a KSEG1 pointer that passes a masked test is then read
// half a gigabyte past the end of what is mapped.
bool valid_ram_address(uint32_t address) {
    return ((address >> 29) == 4u) && ((address & 0x1FFFFFFFu) < 0x00800000u);
}

} // namespace
} // namespace snap

// Whether the main display-list buffer has room for one tag.
//
// The game sized these buffers for its own commands and does not check during
// the frame; past the end it writes into the neighbouring buffer and then the
// matrix heap. The port already emits more display list than the game does, so
// anything added here asks first and simply declines when the answer is no --
// an untagged sprite is drawn exactly as it is today.
static bool snap_rect_tag_fits(uint8_t* rdram, uint32_t cursor) {
    const uint32_t context = MEM_W(0, (gpr)(int32_t)snap::GtlContextId);
    if (context >= 2u) {
        return false;
    }

    const uint32_t entry = snap::GtlDLBuffers + ((context * snap::BufferKinds) + 0u) * snap::DLBufferSize;
    const uint32_t start = MEM_W(0, (gpr)(int32_t)entry);
    const uint32_t capacity = MEM_W(0x4, (gpr)(int32_t)entry);
    if ((start == 0u) || (capacity == 0u) || (cursor < start)) {
        return false;
    }

    const uint32_t used = cursor - start;
    if (used > capacity) {
        return false;
    }

    // Leave the game its own margin as well as room for the tag: the sprite
    // about to draw still has to fit after this.
    return (capacity - used) > (snap::TagBytes + 64u);
}

// Writes the enable words and one group command at the cursor, and returns the
// position after them. The enable is repeated rather than assumed: it costs
// eight bytes and removes any dependence on a camera having run first, which
// would otherwise leave an unknown opcode in the list.
static uint32_t snap_write_rect_tag(uint8_t* rdram, uint32_t cursor, uint32_t id) {
    MEM_W(0x0, (gpr)(int32_t)cursor) = snap::EnableWord0;
    MEM_W(0x4, (gpr)(int32_t)cursor) = snap::EnableWord1;
    MEM_W(0x8, (gpr)(int32_t)cursor) = snap::RectGroupWord0;
    MEM_W(0xC, (gpr)(int32_t)cursor) = id;
    return cursor + snap::TagBytes;
}

// One sprite. a0 is the Sprite, which lives inside the SObj that owns it, and
// that SObj is what the rectangles this draws belong to.
extern "C" void spX2Draw(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t sprite = static_cast<uint32_t>(ctx->r4);

    if (snap::valid_ram_address(sprite)) {
        // Hidden sprites draw nothing and return before writing the cursor
        // back, so a tag written here would be left behind and the caller
        // would then move the list pointer to the middle of it.
        const uint32_t flags = static_cast<uint32_t>(MEM_HU(snap::SpriteFlagsOffset, (gpr)(int32_t)sprite));
        if ((flags & snap::SpriteHiddenFlag) == 0u) {
            const uint32_t cursor = static_cast<uint32_t>(MEM_W(snap::SpriteCursorOffset, (gpr)(int32_t)sprite));
            if (snap::valid_ram_address(cursor) && snap_rect_tag_fits(rdram, cursor)) {
                const uint32_t id = sprite - snap::SObjFromSprite;
                MEM_W(snap::SpriteCursorOffset, (gpr)(int32_t)sprite) =
                    static_cast<int32_t>(snap_write_rect_tag(rdram, cursor, id));
            }
        }
    }

    __real_spX2Draw(rdram, ctx);
}

// One object's whole sprite list. Closing the group here means the rectangles
// that follow -- anything the game draws that is not a sprite -- carry no name
// and are left exactly where they were put.
extern "C" void renDrawSprite(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t cursor = static_cast<uint32_t>(MEM_W(0, (gpr)(int32_t)snap::GMainGfxPos));
    if (snap::valid_ram_address(cursor) && snap_rect_tag_fits(rdram, cursor)) {
        MEM_W(0, (gpr)(int32_t)snap::GMainGfxPos) =
            static_cast<int32_t>(snap_write_rect_tag(rdram, cursor, 0u));
    }

    __real_renDrawSprite(rdram, ctx);
}
