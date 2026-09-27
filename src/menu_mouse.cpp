/**
 * @file menu_mouse.cpp
 * @brief The mouse on the game's photo screens: the album, the PKMN Report,
 * Oak's photo check, the Gallery and the pick between two photos.
 *
 * The title's list, the lab's panel, the pause menu and the Options pages
 * take the mouse in the game's own code (patches/src/graphics_menu_patch.c,
 * snap_mouse_take). The photo screens are larger and keep some of their
 * state in their functions' own statics, so their navigation functions are
 * not copied into the patches; they are intercepted instead (hook_funcs.py
 * renames each to __real_<name>, and the port's <name> below runs first).
 * Each is called by its screen every frame with the frame's input record
 * and pointers to the selection -- a panel slot, or a photo's column and
 * row -- and once with no record when the screen enters that state.
 *
 * Before the game's own body runs, the pointer moving onto a button or a
 * photo moves the selection there, and the body then does what it does
 * for a move of the stick (the corner marks, the sound, the photo's
 * comment). A click on an item is A on it and the right button is B: both
 * are pressed on the next controller reading (input_inject_press), so the
 * screen's own edge detection sees one press whichever order it reads its
 * buttons in. The wheel steps as the stick's up and down do. The mouse is
 * taken only with the live record; the lab passes an empty one while Oak
 * talks, and a click then does nothing, as a press would.
 *
 * The mailbox's mouse block (the byte map in graphics_menu_patch.c) is
 * what the host's input writes once a reading (src/input.cpp,
 * publish_pointer); these read it as the patches do, and raise its claim,
 * which keeps the mouse's buttons out of the controller while a screen
 * handles them.
 */

#include <chrono>
#include <cstdint>

#include "recomp.h"
#include "input.h"

extern "C" recomp_func_t* get_function(int32_t addr);

namespace {

constexpr uint32_t kMailPointer   = 0x80C00088;  // x << 16 | y, all ones off the picture
constexpr uint32_t kMailCounts    = 0x80C0008C;  // moved, click, back, wheel up
constexpr uint32_t kMailWheelDown = 0x80C00090;
constexpr uint32_t kMailClaim     = 0x80C00091;

constexpr uint32_t kLiveRecord = 0x800BEDF8;     // D_800BEDF8[0], src/app_render/53AD0.c
constexpr uint32_t kAuPlaySound = 0x800228E4;

constexpr uint32_t kStickSlowUp    = 0x10000;
constexpr uint32_t kStickSlowDown  = 0x20000;
constexpr uint32_t kStickSlowRight = 0x40000;
constexpr uint32_t kStickSlowLeft  = 0x80000;
constexpr uint16_t kButtonA = 0x8000;
constexpr uint16_t kButtonB = 0x4000;
constexpr int32_t kButtonNone = 35;              // BUTTON_NONE, an empty panel slot

enum : int { Moved = 1, Click = 2, Back = 4, Up = 8, Down = 16 };

uint8_t rd8(uint8_t* rdram, uint32_t a) { return rdram[(a - 0x80000000u) ^ 3u]; }
void wr8(uint8_t* rdram, uint32_t a, uint8_t v) { rdram[(a - 0x80000000u) ^ 3u] = v; }
uint32_t rd32(uint8_t* rdram, uint32_t a) { return *reinterpret_cast<uint32_t*>(rdram + (a - 0x80000000u)); }
void wr32(uint8_t* rdram, uint32_t a, uint32_t v) { *reinterpret_cast<uint32_t*>(rdram + (a - 0x80000000u)) = v; }

// The counts a screen has seen, taken when it enters the state -- or when
// its function runs after a pause, since not every screen calls it with no
// record first: the Report's panel did not, and the click that opened the
// Report, still new to it, chose the panel's button under the pointer.
struct Seen {
    uint8_t moved = 0, click = 0, back = 0, wup = 0, wdn = 0;
    int64_t last_us = 0;
};

int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

void begin(uint8_t* rdram, Seen& s) {
    s.last_us = now_us();
    const uint32_t c = rd32(rdram, kMailCounts);
    s.moved = uint8_t(c >> 24);
    s.click = uint8_t(c >> 16);
    s.back = uint8_t(c >> 8);
    s.wup = uint8_t(c);
    s.wdn = rd8(rdram, kMailWheelDown);
    wr8(rdram, kMailClaim, 4);
}

int take(uint8_t* rdram, Seen& s) {
    const int64_t t = now_us();
    const bool fresh = (t - s.last_us) > 200000;
    s.last_us = t;
    if (fresh) {
        begin(rdram, s);
        return 0;
    }
    wr8(rdram, kMailClaim, 4);
    const uint32_t c = rd32(rdram, kMailCounts);
    const uint8_t now[5] = {uint8_t(c >> 24), uint8_t(c >> 16), uint8_t(c >> 8), uint8_t(c),
                            rd8(rdram, kMailWheelDown)};
    uint8_t* seen[5] = {&s.moved, &s.click, &s.back, &s.wup, &s.wdn};
    int ev = 0;
    for (int i = 0; i < 5; i++) {
        if (now[i] != *seen[i]) {
            *seen[i] = now[i];
            ev |= (1 << i);
        }
    }
    return ev;
}

bool in(uint8_t* rdram, int x, int y, int w, int h) {
    const uint32_t p = rd32(rdram, kMailPointer);
    if (p == 0xFFFFFFFFu) {
        return false;
    }
    const int px = int16_t(p >> 16);
    const int py = int16_t(p & 0xFFFF);
    return (px >= x) && (px < x + w) && (py >= y) && (py < y + h);
}

void or_pressed(uint8_t* rdram, uint32_t record, uint32_t bits) {
    wr32(rdram, record + 0x18, rd32(rdram, record + 0x18) | bits);   // pressedButtons
}

// The game's own sound, through the runtime's table, so the port's volume
// scaling (its replacement of auPlaySound) is the one that runs.
void play(uint8_t* rdram, recomp_context* ctx, uint32_t id) {
    recomp_context c = *ctx;
    c.r4 = int32_t(id);
    get_function(int32_t(kAuPlaySound))(rdram, &c);
}

// A panel of eight button slots (src/window/layout.c: the buttons stand at
// x 0, y 25 + 24i; the corner marks frame 62 by 13 at 22, 29 + 24i).
// buttonsVar holds the screen's UIButton pointer.
void panel(uint8_t* rdram, recomp_context* ctx, Seen& seen, uint32_t buttonsVar, bool sound,
           uint32_t told = 0) {
    const uint32_t record = uint32_t(ctx->r4);
    const uint32_t rowPtr = uint32_t(ctx->r5);
    if (record == 0) {
        begin(rdram, seen);
        return;
    }
    if ((record != kLiveRecord) || (rowPtr == 0)) {
        return;
    }
    const int ev = take(rdram, seen);
    const uint32_t buttons = rd32(rdram, buttonsVar);
    int hit = -1;
    if (buttons != 0) {
        for (int i = 0; i < 8; i++) {
            if ((int32_t(rd32(rdram, buttons + i * 8)) != kButtonNone) && in(rdram, 8, 25 + i * 24, 92, 22)) {
                hit = i;
            }
        }
    }
    if ((ev & (Moved | Click)) && (hit >= 0) && (int32_t(rd32(rdram, rowPtr)) != hit)) {
        wr32(rdram, rowPtr, uint32_t(hit));
        if (told != 0) {
            recomp_context c = *ctx;
            c.r4 = hit;
            get_function(int32_t(told))(rdram, &c);
        }
        if (sound) {
            play(rdram, ctx, 65);
        }
    }
    if ((ev & Click) && (hit >= 0)) {
        snap::input_inject_press(kButtonA);
    }
    if (ev & Back) {
        snap::input_inject_press(kButtonB);
    }
    if (ev & Up) {
        or_pressed(rdram, record, kStickSlowUp);
    }
    if (ev & Down) {
        or_pressed(rdram, record, kStickSlowDown);
    }
}

// A page of photos: six, three across and two down, the corner marks
// framing each at x0 + dx * column, y0 + dy * row -- or the Gallery's four
// of a print. The header's page arrows (src/window/layout.c: the previous
// at x 96, the next at 280) turn the page where the screen has pages, as
// the stick does past the end of a row.
void grid(uint8_t* rdram, recomp_context* ctx, Seen& seen, int x0, int dx, bool pages,
          int cols = 3, int y0 = 56, int dy = 55) {
    const uint32_t record = uint32_t(ctx->r4);
    const uint32_t colPtr = uint32_t(ctx->r5);
    const uint32_t rowPtr = uint32_t(ctx->r6);
    if (record == 0) {
        begin(rdram, seen);
        return;
    }
    if ((record != kLiveRecord) || (colPtr == 0) || (rowPtr == 0)) {
        return;
    }
    const int ev = take(rdram, seen);
    int hitC = -1, hitR = -1;
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < cols; c++) {
            if (in(rdram, x0 + dx * c - 6, y0 + dy * r - 6, dx - 6, dy - 3)) {
                hitC = c;
                hitR = r;
            }
        }
    }
    if ((ev & (Moved | Click)) && (hitC >= 0)) {
        wr32(rdram, colPtr, uint32_t(hitC));
        wr32(rdram, rowPtr, uint32_t(hitR));
    }
    if (ev & Click) {
        if (hitC >= 0) {
            snap::input_inject_press(kButtonA);
        } else if (pages && in(rdram, 86, 0, 40, 40)) {
            wr32(rdram, colPtr, 0);
            or_pressed(rdram, record, kStickSlowLeft);
        } else if (pages && in(rdram, 270, 0, 44, 40)) {
            wr32(rdram, colPtr, uint32_t(cols - 1));
            or_pressed(rdram, record, kStickSlowRight);
        }
    }
    if (ev & Back) {
        snap::input_inject_press(kButtonB);
    }
    if (ev & Up) {
        or_pressed(rdram, record, kStickSlowUp);
    }
    if (ev & Down) {
        or_pressed(rdram, record, kStickSlowDown);
    }
}

Seen g_album_panel, g_album_photos, g_album_drag;
Seen g_report_panel, g_report_photos, g_report_list;
int g_list_target = -1;     // the Report table's row the pointer asked for
bool g_list_click = false;  // and whether it was clicked: A when the bar is there
int g_list_last = -1;       // the bar's row when a step was asked for
Seen g_check_panel, g_check_photos;
Seen g_gallery_panel, g_gallery_photos, g_gallery_print, g_gallery_place;
Seen g_pick;

}  // namespace

extern "C" {

void __real_album_UpdateButtonSelection(uint8_t* rdram, recomp_context* ctx);
void __real_album_UpdatePhotoSelection(uint8_t* rdram, recomp_context* ctx);
void __real_album_DragPhoto(uint8_t* rdram, recomp_context* ctx);
void __real_func_801E28D8_9D9248(uint8_t* rdram, recomp_context* ctx);
void __real_func_801E2AC0_9D9430(uint8_t* rdram, recomp_context* ctx);
void __real_func_camera_check_801DFA80(uint8_t* rdram, recomp_context* ctx);
void __real_func_camera_check_801DFCD4(uint8_t* rdram, recomp_context* ctx);
void __real_func_801E41FC_993C6C(uint8_t* rdram, recomp_context* ctx);
void __real_func_801E2CF8_9D9668(uint8_t* rdram, recomp_context* ctx);
void __real_func_801DF8A4_9FD564(uint8_t* rdram, recomp_context* ctx);
void __real_func_801DFA94_9FD754(uint8_t* rdram, recomp_context* ctx);
void __real_func_801DFE74_9FDB34(uint8_t* rdram, recomp_context* ctx);
void __real_func_801E006C_9FDD2C(uint8_t* rdram, recomp_context* ctx);

// The album (src/pokemon_album/9ABB50.c): its panel, its pages of photos,
// and a photo being moved to another place.
void album_UpdateButtonSelection(uint8_t* rdram, recomp_context* ctx) {
    panel(rdram, ctx, g_album_panel, 0x80250124 /* album_PanelButtons */, true);
    __real_album_UpdateButtonSelection(rdram, ctx);
}

void album_UpdatePhotoSelection(uint8_t* rdram, recomp_context* ctx) {
    grid(rdram, ctx, g_album_photos, 107, 66, true);
    __real_album_UpdatePhotoSelection(rdram, ctx);
}

void album_DragPhoto(uint8_t* rdram, recomp_context* ctx) {
    grid(rdram, ctx, g_album_drag, 107, 66, true);
    __real_album_DragPhoto(rdram, ctx);
}

// The PKMN Report (src/pokemon_report/9D91C0.c): its panel and a Pokemon's
// photos. Its photo page has no pages to turn: the stick past the left
// edge leaves it, which the arrows must not do.
void func_801E28D8_9D9248(uint8_t* rdram, recomp_context* ctx) {
    panel(rdram, ctx, g_report_panel, 0x80230E14 /* D_80230E14_A27784 */, true);
    __real_func_801E28D8_9D9248(rdram, ctx);
}

void func_801E2AC0_9D9430(uint8_t* rdram, recomp_context* ctx) {
    grid(rdram, ctx, g_report_photos, 107, 69, false);
    __real_func_801E2AC0_9D9430(rdram, ctx);
}

// The Report's table of Pokemon (src/pokemon_report/9D91C0.c, and its
// cursor, func_801DF020_9D5990 in 9D3660.c): ten rows 15 apart from y 67,
// the bar's row on screen at D_80230DB0. The table moves only a row at a
// time (and scrolls at its ends), so the pointer walks the bar there one
// row a frame, as the stick's up and down do, and a click on a row is A
// once the bar has reached it. A row past the last Pokemon stops the walk.
void func_801E2CF8_9D9668(uint8_t* rdram, recomp_context* ctx) {
    constexpr uint32_t kBarRow = 0x80230DB0;
    const uint32_t record = uint32_t(ctx->r4);
    if (record == 0) {
        begin(rdram, g_report_list);
        g_list_target = -1;
        g_list_click = false;
        g_list_last = -1;
    } else if (record == kLiveRecord) {
        const int ev = take(rdram, g_report_list);
        const int bar = int32_t(rd32(rdram, kBarRow));
        int hit = -1;
        for (int r = 0; r < 10; r++) {
            if (in(rdram, 108, 67 + 15 * r - 1, 212, 15)) {
                hit = r;
            }
        }
        if ((ev & Moved) && (hit >= 0)) {
            g_list_target = hit;
            g_list_click = false;
        }
        if ((ev & Click) && (hit >= 0)) {
            g_list_target = hit;
            g_list_click = true;
        }
        if (ev & Back) {
            snap::input_inject_press(kButtonB);
        }
        if (ev & (Up | Down)) {
            g_list_target = -1;
            g_list_click = false;
            or_pressed(rdram, record, (ev & Up) ? kStickSlowUp : kStickSlowDown);
        } else if (g_list_target >= 0) {
            if ((g_list_last == bar) && (g_list_target != bar)) {
                g_list_target = -1;   // the step did not move the bar: the end of the list
                g_list_click = false;
            } else if (g_list_target < bar) {
                or_pressed(rdram, record, kStickSlowUp);
            } else if (g_list_target > bar) {
                or_pressed(rdram, record, kStickSlowDown);
            } else {
                if (g_list_click) {
                    snap::input_inject_press(kButtonA);
                }
                g_list_target = -1;
                g_list_click = false;
            }
        }
        g_list_last = (g_list_target >= 0) ? bar : -1;
    }
    __real_func_801E2CF8_9D9668(rdram, ctx);
}

// Oak's photo check (src/camera_check/87D1A0.c): its panel, which plays
// the move sound only when asked to (its third argument), and the ride's
// photos.
void func_camera_check_801DFA80(uint8_t* rdram, recomp_context* ctx) {
    panel(rdram, ctx, g_check_panel, 0x80249AA8 /* D_camera_check_80249AA8 */, ctx->r6 != 0);
    __real_func_camera_check_801DFA80(rdram, ctx);
}

void func_camera_check_801DFCD4(uint8_t* rdram, recomp_context* ctx) {
    grid(rdram, ctx, g_check_photos, 107, 66, true);
    __real_func_camera_check_801DFCD4(rdram, ctx);
}

// The Gallery (src/gallery/9FD510.c): its panel, whose help line follows
// the selection (func_801DDCA8_9FB968, told the new row as the stick's
// moves tell it), its pages of photos, and a print's four places -- chosen,
// and a photo being moved to another.
void func_801DF8A4_9FD564(uint8_t* rdram, recomp_context* ctx) {
    panel(rdram, ctx, g_gallery_panel, 0x802308A0 /* D_802308A0_A4E560 */, true, 0x801DDCA8);
    __real_func_801DF8A4_9FD564(rdram, ctx);
}

void func_801DFA94_9FD754(uint8_t* rdram, recomp_context* ctx) {
    grid(rdram, ctx, g_gallery_photos, 107, 66, true);
    __real_func_801DFA94_9FD754(rdram, ctx);
}

void func_801DFE74_9FDB34(uint8_t* rdram, recomp_context* ctx) {
    grid(rdram, ctx, g_gallery_print, 140, 67, false, 2, 56, 56);
    __real_func_801DFE74_9FDB34(rdram, ctx);
}

void func_801E006C_9FDD2C(uint8_t* rdram, recomp_context* ctx) {
    grid(rdram, ctx, g_gallery_place, 140, 67, false, 2, 56, 56);
    __real_func_801E006C_9FDD2C(rdram, ctx);
}

// The pick between two photos (src/photo_check/993C50.c): the corner marks
// frame 54 by 41 at x 60 + 147i, y 39.
void func_801E41FC_993C6C(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t record = uint32_t(ctx->r4);
    const uint32_t indexPtr = uint32_t(ctx->r5);
    if (record == 0) {
        begin(rdram, g_pick);
    } else if ((record == kLiveRecord) && (indexPtr != 0)) {
        const int ev = take(rdram, g_pick);
        int hit = -1;
        for (int i = 0; i < 2; i++) {
            if (in(rdram, 60 + 147 * i - 6, 39 - 6, 54 + 12, 41 + 12)) {
                hit = i;
            }
        }
        if ((ev & (Moved | Click)) && (hit >= 0)) {
            wr32(rdram, indexPtr, uint32_t(hit));
        }
        if ((ev & Click) && (hit >= 0)) {
            snap::input_inject_press(kButtonA);
        }
        if (ev & Back) {
            snap::input_inject_press(kButtonB);
        }
    }
    __real_func_801E41FC_993C6C(rdram, ctx);
}

}  // extern "C"
