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
 * "the nearest similar rectangle" pairs it with its neighbor. The result was
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
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <unordered_map>

#include "recomp.h"
#include "hle/rt64_snap_diag.h"
#include "settings.h"

extern "C" {
#include "funcs.h"
}

namespace snap {
// overlay_hook.cpp: true while a course's code overlay is resident.
extern std::atomic<bool> g_app_level_resident;

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

// The rectangle alignment command, whose offsets the renderer adds to every
// rectangle that follows: no origin, and a signed offset per edge in quarter
// pixels.
constexpr uint32_t RectAlignV1 = 0x000006;
constexpr uint32_t OriginNone = 0x800;
constexpr uint32_t RectAlignWord0 = Param(ExtendedOpcode, 8, 24) | Param(RectAlignV1, 24, 0);
constexpr uint32_t RectAlignWord1 = Param(OriginNone, 12, 0) | Param(OriginNone, 12, 12);
constexpr uint32_t WholeOpenBytes = 24;   // the enable, then the alignment
constexpr uint32_t WholeCloseBytes = 16;  // the alignment set back to nothing

// Where each copy of the sprite library keeps the box it clips to in its own
// code: scissor_xmax, scissor_ymax, scissor_xmin and scissor_ymin in the
// resident copy (320 by 240 unless a camera sets less), D_803A6648 onward in
// the menu overlay's (xmin, ymin, xmax, ymax; 640 by 480 as a rule, so only
// its top and left ever cut).
struct ClipBox {
    uint32_t xmin;
    uint32_t ymin;
    uint32_t xmax;
    uint32_t ymax;
};
constexpr ClipBox ResidentClip = { 0x80097D48, 0x80097D4C, 0x80097D40, 0x80097D44 };
constexpr ClipBox MenuClip = { 0x803A6648, 0x803A664C, 0x803A6650, 0x803A6654 };

// How far past the edge a sprite is still drawn whole: further than any of
// them travels in a frame, so the frame after it has left still has it.
constexpr int32_t WholeMargin = 64;

// The course's icons that leave sideways when the viewfinder comes up and
// come back when it goes: Icons_IconObjects in the course's code (x, y and
// the SObj, 0x18 bytes each), of which the zoom, the dash engine, its
// viewfinder twin and the zoom-off icon slide along x. Each rests at 268 and
// is hidden at 316, 48 pixels on, where four of its pixels still show.
constexpr uint32_t IconObjects = 0x8038812C;
constexpr uint32_t IconObjectSize = 0x18;
constexpr uint32_t IconObjectSObj = 0x8;
constexpr uint32_t SideIcons[] = { 3, 4, 5, 7 };
constexpr int32_t SideIconRest = 268;
constexpr int32_t SideIconTravel = 48;

// The film counter: the pointer the course keeps to its object
// (D_803B09D8), which owns the canister with its strip and the three digits.
// An SObj names its owner at +0x4.
constexpr uint32_t FilmCounterObject = 0x803B09D8;

// The word PAUSE (Pause_LabelPause, the sprite object the pause code keeps):
// the game starts it at 360, forty pixels past the 4:3 edge, and slides it
// to 111 in steps of 24.
constexpr uint32_t PauseWordSObj = 0x80382C78;
constexpr int32_t PauseWordStart = 360;
constexpr int32_t PauseWordRest = 111;
constexpr uint32_t SObjOwner = 0x4;

// The right edge of the scene as the viewfinder insets it
// (MainCameraBorderXmax): 320 with the viewfinder down, 290 with it up.
constexpr uint32_t ViewBorderXmax = 0x803AE548;
constexpr int32_t ViewFullInset = 30;

// The slides the game keeps finer than a pixel (patches/src/
// check_slide_patch.c): two mailbox words, each a sprite object in its low
// 24 bits and, above them, four more than the quarter pixels its picture
// stands past the sprite's whole pixel. Zero is no slide.
constexpr uint32_t FineSlots = 0x80C000E0;
constexpr uint32_t FineSlotCount = 2;

// A sprite slot's address is not enough to name it by itself.
//
// The decompiled allocator (src/sys/om.c in the decomp: omGetSObj and
// omFreeSObj) is a LIFO free list -- freeing pushes onto the head and
// allocating pops it straight back, so the most recently destroyed sprite's
// address is the very next one handed out. Effects churn constantly: a leaf
// despawns, another spawns, and the new one takes the dead one's address
// within a frame or two. Named by address alone the new sprite inherits the
// old one's identity and pairs with its previous position, drawn sliding out
// of somewhere it never was -- the ghosting that survived every effect-system
// fix, because it lives in the OTHER tagged path.
//
// The same hazard was already found and closed twice in this port, for object
// matrices (the omGetMtx serial) and for particles (fx_tags.cpp). This is the
// third copy of the same lesson: any identity taken from a recycling allocator
// needs a generation number beside the address.
//
// Touched only on the game's own thread, at creation and at drawing.
std::unordered_map<uint32_t, uint32_t> g_sobj_serials;
uint32_t g_next_sobj_serial = 0;

uint32_t sobj_id(uint32_t sobj) {
    uint32_t serial = 0;
    const auto it = g_sobj_serials.find(sobj);
    if (it != g_sobj_serials.end()) {
        serial = it->second;
    }

    uint32_t id = sobj ^ (serial * 2654435761u);
    id ^= (id >> 13);
    if ((id == 0u) || (id == 0xFFFFFFFFu)) {
        id = 1u;
    }

    return id;
}

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
// the frame; past the end it writes into the neighboring buffer and then the
// matrix heap. The port already emits more display list than the game does, so
// anything added here asks first and simply declines when the answer is no --
// an untagged sprite is drawn exactly as it is today.
static bool snap_dl_room(uint8_t* rdram, uint32_t cursor, uint32_t bytes) {
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

    // Leave the game its own margin as well as room for what is added: the
    // sprite about to draw still has to fit after this.
    return (capacity - used) > (bytes + 64u);
}

static bool snap_rect_tag_fits(uint8_t* rdram, uint32_t cursor) {
    return snap_dl_room(rdram, cursor, snap::TagBytes);
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

// Counts the rectangle commands in a span of display list, so the effect of
// naming a path can be read from the same number that identified it. Walked at
// command alignment; a texture rectangle is more than one word and the words
// after the first are coordinates, so this compares paths against each other
// rather than being an exact total.
static uint32_t snap_count_window_rects(uint8_t* rdram, uint32_t from, uint32_t to) {
    if (!snap::valid_ram_address(from) || !snap::valid_ram_address(to) || (to <= from)) {
        return 0;
    }

    if ((to - from) > 0x8000u) {
        return 0;
    }

    uint32_t count = 0;
    for (uint32_t cursor = from; (cursor + 8u) <= to; cursor += 8u) {
        const uint32_t opcode = static_cast<uint32_t>(MEM_W(0, (gpr)(int32_t)cursor)) >> 24;
        if ((opcode == 0xE4u) || (opcode == 0xE5u) || (opcode == 0xF6u)) {
            count++;
        }
    }

    return count;
}

// A sprite that hangs over an edge of the screen, drawn whole.
//
// A texture rectangle cannot say a negative coordinate, so the sprite library
// clips in its own code: a strip that crosses the edge of its clip box is cut
// to what shows, and one that is wholly past it is not drawn. A picture
// sliding out therefore loses each strip a frame before the strip is gone
// from view, and the frames in between have nothing to draw it with: the
// course monitor rolled down smoothly and rolled up in the game's own steps
// (seen by hand, Oct 4 2026; the slide in rt64_game_frame.cpp covers the
// way in and cannot cover the way out).
//
// So the library is handed the sprite moved until it is inside the box,
// where it draws every strip whole, and the renderer is told to move the
// rectangles back by the same distance: they land where the game put the
// sprite, past the edge, and the screen's own scissor cuts them where the
// library would have. The picture is the same; the strips are all there,
// the same size and the same count in every frame, and they pair by their
// ordinals in both directions.
//
// Only while a picture is being placed between frames; only when the box is
// the screen (a smaller one is a crop the scissor does not know); only
// within WholeMargin of the edge. And sideways only where the picture
// reaches the window's sides: in a wide window a course's picture does, so
// the word PAUSE now leaves through the margin and comes in through it, but
// a menu's 4:3 picture has black beside it, and a sprite drawn whole there
// would show on the black.
struct SnapWhole {
    int32_t dx = 0;
    int32_t dy = 0;
    int32_t slide = 0;
    int32_t fine = 0;
};

// The quarter pixels the group being drawn stands past its whole pixel, set
// by the menu library's group draw below for the length of that draw. Only
// the game's own thread draws.
static int32_t g_fine_quarters = 0;

// A slide the game eases in a float reaches the sprite in whole pixels, and
// its slow end is a pixel, a wait, a pixel. Where the patch leaves the rest
// of the float, the group is drawn that much further along: the same
// alignment that draws a sprite whole carries it, a quarter pixel at a time.
// Only while pictures are being placed between frames.
static int32_t snap_fine_quarters(uint8_t* rdram, uint32_t sobj) {
    if ((snap::settings().fps_mode == 0) || !snapdiag::rectInterpolationEnabled().load(std::memory_order_relaxed)) {
        return 0;
    }

    for (uint32_t slot = 0; slot < snap::FineSlotCount; slot++) {
        const uint32_t word = static_cast<uint32_t>(MEM_W(0, (gpr)(int32_t)(snap::FineSlots + slot * 4u)));
        if ((word == 0u) || ((0x80000000u | (word & 0x00FFFFFFu)) != sobj)) {
            continue;
        }

        const int32_t quarters = static_cast<int32_t>(word >> 24) - 4;
        return ((quarters >= -3) && (quarters <= 3)) ? quarters : 0;
    }

    return 0;
}

static bool snap_dl_room(uint8_t* rdram, uint32_t cursor, uint32_t bytes);

// How much further right a sprite of the course's display is drawn in a wide
// picture: the two things there that belong to the screen's right edge.
//
// The film counter's strip runs off the edge of the game's 4:3 screen: its
// picture is 63 pixels wide and ends at 320. In a wide picture that edge is
// in the middle of the view and the strip stopped in mid-air, so the counter
// is drawn the width of the margin further right, against the real edge.
// The viewfinder closes the wide picture to the game's own frame behind
// black bars (patches/src/border_patch.c), and the counter comes in with the
// bars: it stays as far from the scene's edge as the cartridge has it, and
// with the viewfinder up it is where the game put it.
//
// The side icons rest where the game put them. The game slides them 48
// pixels to its own edge and hides them there: in a wide picture they
// stopped short of the side and vanished, and came back the same way (seen
// by hand, Oct 4 2026). Their way out is stretched by the width of the
// margin: an icon covers the longer distance in the game's own eight steps
// and is hidden with the same four pixels showing at the real edge. At rest
// the stretch is nothing, and it is taken from where the game keeps the
// icon, not from the sprite, so the dash engine's shake is not stretched.
//
// The widening is above one only in a course with Widescreen on, and it is
// published a tick behind the scene: the tables read here are the course's
// own, so the course's code must be loaded too. The margin is rounded up, so
// the strip reaches the edge and does not stop a hair short of it.
static int32_t snap_wide_shift(uint8_t* rdram, uint32_t sprite) {
    const uint32_t q8 = snap::view_wide_q8();
    if ((q8 <= 256u) || !snap::g_app_level_resident.load(std::memory_order_relaxed)) {
        return 0;
    }
    const int32_t margin = static_cast<int32_t>((160u * (q8 - 256u) + 255u) / 256u);

    // The word PAUSE comes in from the right. The game starts it forty
    // pixels past the 4:3 edge, which in a wide picture is inside the view,
    // so at 16:9 it popped in thirteen pixels from the edge. Its way in is
    // stretched by the margin, as the icons' way out is: at its start it
    // stands the margin further right, at its rest where the game put it.
    {
        const uint32_t pauseWord = static_cast<uint32_t>(MEM_W(0, (gpr)(int32_t)snap::PauseWordSObj));
        if (snap::valid_ram_address(pauseWord) && ((sprite - snap::SObjFromSprite) == pauseWord)) {
            const int32_t wordX = static_cast<int16_t>(MEM_H(0x0, (gpr)(int32_t)sprite));
            if ((wordX > snap::PauseWordRest) && (wordX <= snap::PauseWordStart)) {
                return (margin * (wordX - snap::PauseWordRest)) / (snap::PauseWordStart - snap::PauseWordRest);
            }
            return 0;
        }
    }
    if (!snap::settings().wide_hud) {
        return 0;       // the Wide HUD row Off: both where the cartridge has them
    }
    const uint32_t film = static_cast<uint32_t>(MEM_W(0, (gpr)(int32_t)snap::FilmCounterObject));
    if (snap::valid_ram_address(film) &&
        (static_cast<uint32_t>(MEM_W(snap::SObjOwner, (gpr)(int32_t)(sprite - snap::SObjFromSprite))) == film)) {
        int32_t inset = 320 - static_cast<int32_t>(MEM_W(0, (gpr)(int32_t)snap::ViewBorderXmax));
        inset = (inset < 0) ? 0 : ((inset > snap::ViewFullInset) ? snap::ViewFullInset : inset);
        return (margin * (snap::ViewFullInset - inset)) / snap::ViewFullInset;
    }

    for (const uint32_t icon : snap::SideIcons) {
        const uint32_t entry = snap::IconObjects + icon * snap::IconObjectSize;
        const uint32_t sobj = static_cast<uint32_t>(MEM_W(snap::IconObjectSObj, (gpr)(int32_t)entry));
        if ((sobj + snap::SObjFromSprite) != sprite) {
            continue;
        }

        const int32_t out = static_cast<int32_t>(MEM_W(0x0, (gpr)(int32_t)entry)) - snap::SideIconRest;
        if ((out <= 0) || (out > (snap::SideIconTravel * 2))) {
            return 0;
        }

        return (out * margin) / snap::SideIconTravel;
    }

    return 0;
}

static bool snap_picture_reaches_sides() {
    if (snap::view_wide_q8() > 256u) {
        return true;
    }

    const snapdiag::PresentedView view = snapdiag::presentedView();
    return !((view.height > 0.0f) && ((view.width / view.height) > 1.34f));
}

static SnapWhole snap_whole_open(uint8_t* rdram, uint32_t sprite, const snap::ClipBox& clip) {
    SnapWhole whole;
    if (!snap::valid_ram_address(sprite)) {
        return whole;
    }

    const uint32_t flags = static_cast<uint32_t>(MEM_HU(snap::SpriteFlagsOffset, (gpr)(int32_t)sprite));
    if ((flags & snap::SpriteHiddenFlag) != 0u) {
        return whole;
    }

    // The wide picture's shift is the picture's width, not the frame rate's:
    // it is taken at any rate. The rest is only for a picture being placed
    // between frames.
    const int32_t slide = snap_wide_shift(rdram, sprite);
    if ((slide == 0) && ((snap::settings().fps_mode == 0) ||
        !snapdiag::rectInterpolationEnabled().load(std::memory_order_relaxed))) {
        return whole;
    }

    const int32_t xmin = MEM_W(0, (gpr)(int32_t)clip.xmin);
    const int32_t ymin = MEM_W(0, (gpr)(int32_t)clip.ymin);
    const int32_t xmax = MEM_W(0, (gpr)(int32_t)clip.xmax);
    const int32_t ymax = MEM_W(0, (gpr)(int32_t)clip.ymax);
    if ((xmin != 0) || (ymin != 0) || (xmax < 320) || (ymax < 240)) {
        return whole;
    }

    // The sprite's size on screen: its own, by its scale, and a pixel over
    // for what the library rounds.
    const int32_t gameX = static_cast<int16_t>(MEM_H(0x0, (gpr)(int32_t)sprite));
    const int32_t x = gameX + slide;
    const int32_t y = static_cast<int16_t>(MEM_H(0x2, (gpr)(int32_t)sprite));
    const int32_t width = static_cast<int16_t>(MEM_H(0x4, (gpr)(int32_t)sprite));
    const int32_t height = static_cast<int16_t>(MEM_H(0x6, (gpr)(int32_t)sprite));
    union { uint32_t u; float f; } scaleX, scaleY;
    scaleX.u = static_cast<uint32_t>(MEM_W(0x8, (gpr)(int32_t)sprite));
    scaleY.u = static_cast<uint32_t>(MEM_W(0xC, (gpr)(int32_t)sprite));
    if (!(scaleX.f > 0.0f) || !(scaleY.f > 0.0f) || (scaleX.f > 16.0f) || (scaleY.f > 16.0f) || (width <= 0) || (height <= 0)) {
        return whole;
    }

    const int32_t right = x + static_cast<int32_t>(static_cast<float>(width) * scaleX.f);
    const int32_t bottom = y + static_cast<int32_t>(static_cast<float>(height) * scaleY.f);
    if ((right <= -snap::WholeMargin) || (bottom <= -snap::WholeMargin) ||
        (gameX >= (xmax + snap::WholeMargin)) || (y >= (ymax + snap::WholeMargin))) {
        return whole;
    }

    // How far to move it to sit inside the box, and a pixel further in on
    // the far side for what the library rounds. One that overhangs two
    // opposite edges cannot, and one the move would push past the near edge
    // cannot either; both are left to the library. A sprite that fills the
    // box exactly overhangs nothing: counted as one that did, the photo
    // check's backdrop was moved a pixel and lost its first row and column
    // (seen by hand, Oct 4 2026).
    int32_t dx = 0;
    int32_t dy = 0;
    if ((x < 0) != (right > xmax)) {
        dx = (x < 0) ? -x : (xmax - right - 1);
    }
    else if (x < 0) {
        return whole;
    }
    if ((y < 0) != (bottom > ymax)) {
        dy = (y < 0) ? -y : (ymax - bottom - 1);
    }
    else if (y < 0) {
        return whole;
    }
    if (((x + dx) < 0) || ((y + dy) < 0)) {
        return whole;
    }
    if ((dx != 0) && !snap_picture_reaches_sides()) {
        dx = 0;
    }

    // With nothing over an edge and no part of a pixel to add, or no room in
    // the list for the commands, a shifted sprite is still drawn where the
    // wide picture puts it.
    const int32_t fine = g_fine_quarters;
    const uint32_t cursor = static_cast<uint32_t>(MEM_W(snap::SpriteCursorOffset, (gpr)(int32_t)sprite));
    if (((dx == 0) && (dy == 0) && (fine == 0)) || !snap::valid_ram_address(cursor) ||
        !snap_dl_room(rdram, cursor, snap::WholeOpenBytes + snap::TagBytes + snap::WholeCloseBytes)) {
        if (slide != 0) {
            whole.slide = slide;
            MEM_H(0x0, (gpr)(int32_t)sprite) = static_cast<int16_t>(x);
        }
        return whole;
    }

    whole.dx = dx;
    whole.dy = dy;
    whole.slide = slide;
    whole.fine = fine;
    const uint32_t backX = static_cast<uint32_t>(-whole.dx * 4 + fine) & 0xFFFFu;
    const uint32_t backY = static_cast<uint32_t>(-whole.dy * 4) & 0xFFFFu;
    MEM_W(0x00, (gpr)(int32_t)cursor) = snap::EnableWord0;
    MEM_W(0x04, (gpr)(int32_t)cursor) = snap::EnableWord1;
    MEM_W(0x08, (gpr)(int32_t)cursor) = snap::RectAlignWord0;
    MEM_W(0x0C, (gpr)(int32_t)cursor) = snap::RectAlignWord1;
    MEM_W(0x10, (gpr)(int32_t)cursor) = (backX << 16) | backY;
    MEM_W(0x14, (gpr)(int32_t)cursor) = (backX << 16) | backY;
    MEM_W(snap::SpriteCursorOffset, (gpr)(int32_t)sprite) = static_cast<int32_t>(cursor + snap::WholeOpenBytes);
    MEM_H(0x0, (gpr)(int32_t)sprite) = static_cast<int16_t>(x + whole.dx);
    MEM_H(0x2, (gpr)(int32_t)sprite) = static_cast<int16_t>(y + whole.dy);
    return whole;
}

// Puts the sprite back where the game had it and ends the alignment. The
// library leaves its cursor one command past the last it wrote, an end of
// list its caller steps back over; the alignment's end goes over that
// command, and the cursor one command past it.
static void snap_whole_close(uint8_t* rdram, uint32_t sprite, const SnapWhole& whole) {
    if ((whole.dx == 0) && (whole.dy == 0) && (whole.slide == 0) && (whole.fine == 0)) {
        return;
    }

    MEM_H(0x0, (gpr)(int32_t)sprite) = static_cast<int16_t>(static_cast<int16_t>(MEM_H(0x0, (gpr)(int32_t)sprite)) - whole.dx - whole.slide);
    MEM_H(0x2, (gpr)(int32_t)sprite) = static_cast<int16_t>(static_cast<int16_t>(MEM_H(0x2, (gpr)(int32_t)sprite)) - whole.dy);
    if ((whole.dx == 0) && (whole.dy == 0) && (whole.fine == 0)) {
        return;
    }

    const uint32_t end = static_cast<uint32_t>(MEM_W(snap::SpriteCursorOffset, (gpr)(int32_t)sprite));
    if (!snap::valid_ram_address(end) || !snap::valid_ram_address(end - 8u)) {
        return;
    }

    const uint32_t at = end - 8u;
    MEM_W(0x0, (gpr)(int32_t)at) = snap::RectAlignWord0;
    MEM_W(0x4, (gpr)(int32_t)at) = snap::RectAlignWord1;
    MEM_W(0x8, (gpr)(int32_t)at) = 0;
    MEM_W(0xC, (gpr)(int32_t)at) = 0;
    MEM_W(snap::SpriteCursorOffset, (gpr)(int32_t)sprite) = static_cast<int32_t>(at + snap::WholeCloseBytes + 8u);
}

// One sprite. a0 is the Sprite, which lives inside the SObj that owns it, and
// that SObj is what the rectangles this draws belong to.
//
// Two copies of this library exist. The resident one draws during a course; the
// menu overlay carries its own, and until now only the resident one was tagged,
// which is why the interface screens stepped while the selection bracket beside
// them -- drawn by the resident library -- moved smoothly. Measured on a real
// session, the menu overlay's copy accounted for essentially every unnamed
// rectangle on those screens.
//
// The copies are byte-for-byte the same shape: the hidden flag is at
// Sprite+0x14 with the same bit, the display-list cursor is at Sprite+0x3C, and
// the caller seeds it from gMainGfxPos and takes it back minus eight. So they
// take the same treatment, verified against both rather than assumed from one.
// The ids cannot collide because both libraries draw objects from the same
// allocator, so an SObj address names exactly one sprite whichever copy drew it.
static void snap_tag_sprite(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t sprite = static_cast<uint32_t>(ctx->r4);
    if (!snap::valid_ram_address(sprite)) {
        return;
    }

    // Hidden sprites draw nothing and return before writing the cursor back, so
    // a tag written here would be left behind and the caller would then move
    // the list pointer into the middle of it.
    const uint32_t flags = static_cast<uint32_t>(MEM_HU(snap::SpriteFlagsOffset, (gpr)(int32_t)sprite));
    if ((flags & snap::SpriteHiddenFlag) != 0u) {
        return;
    }

    const uint32_t cursor = static_cast<uint32_t>(MEM_W(snap::SpriteCursorOffset, (gpr)(int32_t)sprite));
    if (!snap::valid_ram_address(cursor) || !snap_rect_tag_fits(rdram, cursor)) {
        return;
    }

    const uint32_t id = snap::sobj_id(sprite - snap::SObjFromSprite);
    MEM_W(snap::SpriteCursorOffset, (gpr)(int32_t)sprite) =
        static_cast<int32_t>(snap_write_rect_tag(rdram, cursor, id));
}

// One object's whole sprite list. Closing the group here means the rectangles
// that follow -- anything drawn that is not a sprite -- carry no name and are
// left exactly where they were put.
static void snap_close_sprite_group(uint8_t* rdram) {
    const uint32_t cursor = static_cast<uint32_t>(MEM_W(0, (gpr)(int32_t)snap::GMainGfxPos));
    if (snap::valid_ram_address(cursor) && snap_rect_tag_fits(rdram, cursor)) {
        MEM_W(0, (gpr)(int32_t)snap::GMainGfxPos) =
            static_cast<int32_t>(snap_write_rect_tag(rdram, cursor, 0u));
    }
}

// The resident library, used during a course.
extern "C" void spX2Draw(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t sprite = static_cast<uint32_t>(ctx->r4);
    const SnapWhole whole = snap_whole_open(rdram, sprite, snap::ResidentClip);
    snap_tag_sprite(rdram, ctx);
    __real_spX2Draw(rdram, ctx);
    snap_whole_close(rdram, sprite, whole);
}

extern "C" void renDrawSprite(uint8_t* rdram, recomp_context* ctx) {
    snap_close_sprite_group(rdram);
    __real_renDrawSprite(rdram, ctx);
}

// The menu overlay's copy, used by the laboratory, the course selector and the
// rest of the interface.
extern "C" void func_80373670_846E20(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t sprite = static_cast<uint32_t>(ctx->r4);
    const SnapWhole whole = snap_whole_open(rdram, sprite, snap::MenuClip);
    snap_tag_sprite(rdram, ctx);
    __real_func_80373670_846E20(rdram, ctx);
    snap_whole_close(rdram, sprite, whole);
}

// Every sprite slot handed out gets a generation number, so a recycled
// address is a different name from the sprite that used to live there. Both
// copies of the sprite library allocate through this one function, so one
// hook covers them both.
extern "C" void omGObjAddSprite(uint8_t* rdram, recomp_context* ctx) {
    __real_omGObjAddSprite(rdram, ctx);

    const uint32_t sobj = static_cast<uint32_t>(ctx->r2);
    if (snap::valid_ram_address(sobj)) {
        snap::g_sobj_serials[sobj] = ++snap::g_next_sobj_serial;
    }
}

// The menu library's draw of a sprite object, its sisters and their children,
// which calls itself for the children: a group whose slide is kept finer
// than a pixel is drawn that much further along, all of it, and the draw it
// was called from gets its own value back.
extern "C" void func_803719B0_845160(uint8_t* rdram, recomp_context* ctx) {
    snap_close_sprite_group(rdram);

    const int32_t fineBefore = g_fine_quarters;
    const int32_t fine = snap_fine_quarters(rdram, static_cast<uint32_t>(ctx->r4));
    if (fine != 0) {
        g_fine_quarters = fine;
    }

    // Still counted, so the effect of naming this path can be read straight off
    // the same line that identified it.
    if (!snapdiag::statsEnabled()) {
        __real_func_803719B0_845160(rdram, ctx);
        g_fine_quarters = fineBefore;
        return;
    }

    if (fine != 0) {
        static uint32_t said = 0;
        if ((said++ % 20u) == 0u) {
            printf("[SNAP-2D] fine slide: sprite object %08X drawn %d quarter pixels past its whole pixel\n",
                static_cast<uint32_t>(ctx->r4), fine);
            fflush(stdout);
        }
    }

    const uint32_t before = static_cast<uint32_t>(MEM_W(0, (gpr)(int32_t)snap::GMainGfxPos));
    __real_func_803719B0_845160(rdram, ctx);
    const uint32_t after = static_cast<uint32_t>(MEM_W(0, (gpr)(int32_t)snap::GMainGfxPos));
    g_fine_quarters = fineBefore;
    snapdiag::rectsFromWindowCounter().fetch_add(
        snap_count_window_rects(rdram, before, after), std::memory_order_relaxed);
}
