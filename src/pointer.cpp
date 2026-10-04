/**
 * @file pointer.cpp
 * @brief The port's own mouse pointer: an arrow carrying a camera lens,
 * JackandBeans's pictures, and the lens's shutter, which closes and opens
 * again on a click and opens as the pointer comes on screen; and the flash
 * the pointer leaves with when it has sat idle.
 *
 * The pictures are menu_text/pointer.png beside the executable (or the
 * player's own copy in the data directory): four frames side by side, the
 * lens open, almost open, half closed and closed, each 23 by 26, pixel art
 * made from JackandBeans's pictures (the scratchpad's make_sheet_smooth.py:
 * an area average with hard edges, the yellow kept where it covers a fair
 * share of a pixel), with the arrow's tip at the frame's top-left corner;
 * outside the lens's glass the four are one picture, so only the shutter
 * moves. Each frame is enlarged here pixel for pixel by a whole number of
 * screen pixels: half the scale the game's picture has on screen (its 240
 * lines over the picture's height in pixels, halved; 2 in a 1280 by 960
 * window, so 46 by 52), and the cursor is kept within 64 pixels on screen,
 * the size a display's hardware cursor handles: at 92 by 104 the picture
 * under it glitched a little as the shutter changed (JackandBeans, Oct 2
 * 2026), as a cursor composed in software does. When the window or the
 * screen changes the scale, the cursors are made again.
 *
 * The flash is menu_text/pointer_flash.png: four frames of 32 by 32 with
 * the pointer set in from the corner (its tip at the frame's center less
 * half the pointer, where the hotspot goes), JackandBeans's comic burst
 * over the whole pointer (make_flash.py), the pointer under the first two
 * and gone from the last two. In fullscreen the pointer hides three seconds
 * after the hand last moved or clicked (input.cpp); in the last four tenths
 * of that second the flash frames show, a tenth each, so the pointer goes
 * out in its own flash, and it comes back the same way in reverse, the
 * burst first and the pointer out of it. Without the file there is no
 * flash, and an appearance opens the lens instead.
 *
 * It is the system's cursor, not a sprite the game draws: the menus run at
 * the cartridge's thirty frames a second, and a pointer drawn by the game
 * would trail the hand by a frame, which is felt at once; the system moves
 * its cursor at the mouse's own rate. The shutter and the flash are the
 * cursor changing between images: on the main thread, where SDL's cursor
 * calls belong, from the event pump (pointer_update) and the mouse's button
 * events (pointer_click). Where the cursor cannot be made, the file is
 * absent, or the setting is off, the system's pointer stays. When it shows
 * and when it hides is unchanged (src/input.cpp): hidden while a course
 * runs and the mouse aims, shown on the menus, and in fullscreen only while
 * it moves.
 *
 * SNAP_POINTER_TEST=1 clicks the shutter by itself every second and a half,
 * plays the flash and a hidden spell once every six seconds, and prints
 * every change of frame and of scale, so the cursor can be photographed
 * off the screen without a hand on the mouse.
 */

#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <SDL2/SDL.h>

// stb_image's implementation is compiled inside the RT64 library; this
// include only brings the declarations.
#include "stb/stb_image.h"

#include "hle/rt64_snap_diag.h"
#include "paths.h"
#include "settings.h"

namespace snap {

namespace {

constexpr int kFrames = 4;       // open, almost open, half closed, closed
constexpr int kFlashFrames = 4;  // the burst small and wide over the pointer, then wide and small alone
constexpr int kGameLines = 240;  // the game's picture, in its own pixels
constexpr int kMaxCursorPx = 64; // a hardware cursor's size; a larger one is composed in software

// A sheet in memory: RGBA, its frames side by side.
struct Sheet {
    std::vector<uint8_t> pixels;
    int width = 0;               // the whole sheet
    int fw = 0;                  // one frame
    int fh = 0;
};

Sheet g_sheet;                   // the pointer's four frames
Sheet g_flash;                   // the flash's four, or empty
SDL_Window* g_window = nullptr;
SDL_Cursor* g_cursor[kFrames] = {};
SDL_Cursor* g_flashCursor[kFlashFrames] = {};
int g_scale = 0;                 // the cursors' scale; 0 before any are made
int g_pendingScale = 0;          // a scale measured but not yet held long enough
int g_pendingCount = 0;
int g_frame = -1;                // the pointer frame the cursor shows; -1 before install or while a flash frame shows
int g_flashShown = -1;           // the flash frame the cursor shows; -1 none
std::atomic<int> g_want{0};      // 1 the port's pointer wanted (the setting), 0 the system's
bool g_on = false;               // the port's pointer shown now
bool g_loadFailed = false;       // the sheet could not be read; no second try

// A click: the shutter closes in three steps, rests closed for two, and
// opens in three, seven tenths of a second in all (30 ms steps were "a bit
// too quick", 50 "way too fast": JackandBeans, Oct 2 and 3 2026). An
// appearance: the flash in reverse, the burst first and the pointer out of
// it (a step of kFlashBase and more is a flash frame), or without a flash
// sheet the lens opening from closed.
constexpr int kFlashBase = 100;
const int kClick[] = {1, 2, 3, 3, 2, 1, 0};
const int kAppear[] = {3, 2, 1, 0};
const int kFlashIn[] = {kFlashBase + 3, kFlashBase + 2, kFlashBase + 1, kFlashBase + 0, 0};
constexpr uint32_t kClickStepMs = 100;
constexpr uint32_t kAppearStepMs = 120;
// The flash out: its four frames a tenth of a second each, ending as the
// pointer hides.
constexpr int32_t kFlashStepMs = 100;
constexpr int32_t kFlashMs = kFlashStepMs * kFlashFrames;

const int* g_seq = nullptr;      // the sequence playing, or none
int g_seqLen = 0;
uint32_t g_seqStepMs = 0;
uint32_t g_seqStartMs = 0;
bool g_visible = false;
int32_t g_hideMsLeft = INT32_MAX;  // how long until fullscreen hides the pointer; INT32_MAX when it will not
bool g_test = false;
uint32_t g_testAtMs = 0;

// The pointer's size on screen at a scale.
void target_size(int scale, int& dw, int& dh) {
    dw = std::max(1, g_sheet.fw * scale);
    dh = std::max(1, g_sheet.fh * scale);
}

// stb_image hands a narrow file name to fopen, which on Windows is the ANSI
// code page (menu_assets.cpp opens its files the same way).
stbi_uc* load_png(const std::filesystem::path& path, int* w, int* h) {
#if defined(_WIN32)
    FILE* f = _wfopen(path.c_str(), L"rb");
#else
    FILE* f = fopen(path.c_str(), "rb");
#endif
    if (f == nullptr) {
        return nullptr;
    }
    int comp = 0;
    stbi_uc* data = stbi_load_from_file(f, w, h, &comp, 4);
    fclose(f);
    return data;
}

// A sheet of `frames` frames in a row from menu_text; false, with a line,
// when it is absent or not that.
bool load_sheet_file(const char* name, int frames, Sheet& out) {
    int w = 0;
    int h = 0;
    stbi_uc* data = load_png(asset_path(std::string("menu_text/") + name), &w, &h);
    if (data == nullptr) {
        printf("[SNAP] pointer: menu_text/%s not found or not a PNG\n", name);
        fflush(stdout);
        return false;
    }
    if ((w % frames != 0) || (w < frames) || (h < 1) || (w / frames > 512) || (h > 512)) {
        printf("[SNAP] pointer: menu_text/%s is %d by %d, not %d frames of up to 512 by 512 in a row\n", name, w, h,
               frames);
        fflush(stdout);
        stbi_image_free(data);
        return false;
    }
    out.pixels.assign(data, data + size_t(w) * size_t(h) * 4);
    stbi_image_free(data);
    out.width = w;
    out.fw = w / frames;
    out.fh = h;
    return true;
}

// Screen pixels per pointer pixel: half the game's own scale (the presented
// picture's height over the game's 240 lines; the renderer publishes the
// picture's rectangle at every present, and before the first it is the
// window's 4:3 fit), to the nearest whole number so every pointer pixel is
// the same size, and never so many that a cursor passes 64 pixels.
int pointer_scale() {
    const snapdiag::PresentedView v = snapdiag::presentedView();
    float s = 0.0f;
    if ((v.height > 0.0f) && (v.fbHeight > 0.0f)) {
        s = v.height / v.fbHeight;
    }
    if (!(s > 0.0f) && (g_window != nullptr)) {
        int ww = 0;
        int wh = 0;
        SDL_GetWindowSizeInPixels(g_window, &ww, &wh);
        s = float(std::min(wh, (ww * 3) / 4)) / float(kGameLines);
    }
    const int largest = std::max({1, g_sheet.fw, g_sheet.fh, g_flash.fw, g_flash.fh});
    const int most = std::max(1, kMaxCursorPx / largest);
    return std::clamp(int(std::lround(s * 0.5f)), 1, most);
}

// One frame of a sheet, each of its pixels a block of scale by scale screen
// pixels, the hotspot (the arrow's tip) at (hx, hy) of the frame.
SDL_Cursor* make_cursor(const Sheet& sheet, int frame, int scale, int hx, int hy) {
    const int dw = sheet.fw * scale;
    const int dh = sheet.fh * scale;
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, dw, dh, 32, SDL_PIXELFORMAT_RGBA32);
    if (surface == nullptr) {
        return nullptr;
    }
    uint8_t* out = static_cast<uint8_t*>(surface->pixels);
    for (int y = 0; y < dh; y++) {
        uint8_t* o = out + size_t(y) * size_t(surface->pitch);
        const uint8_t* row =
            sheet.pixels.data() + (size_t(y / scale) * size_t(sheet.width) + size_t(frame) * size_t(sheet.fw)) * 4;
        for (int x = 0; x < dw; x++) {
            const uint8_t* p = row + size_t(x / scale) * 4;
            o[0] = p[0];
            o[1] = p[1];
            o[2] = p[2];
            o[3] = p[3];
            o += 4;
        }
    }
    SDL_Cursor* cursor = SDL_CreateColorCursor(surface, hx * scale, hy * scale);
    SDL_FreeSurface(surface);
    return cursor;
}

void free_cursors(SDL_Cursor** cursors, int n) {
    for (int i = 0; i < n; i++) {
        if (cursors[i] != nullptr) {
            SDL_FreeCursor(cursors[i]);
            cursors[i] = nullptr;
        }
    }
}

// The cursors at a scale, the pointer's four and the flash's, replacing the
// ones before; the cursor shown stays on its frame.
bool build(int scale) {
    SDL_Cursor* made[kFrames] = {};
    SDL_Cursor* flash[kFlashFrames] = {};
    bool ok = true;
    for (int i = 0; i < kFrames; i++) {
        made[i] = make_cursor(g_sheet, i, scale, 0, 0);
        ok = ok && (made[i] != nullptr);
    }
    if (!g_flash.pixels.empty()) {
        // The pointer sits centered in a flash frame: its tip, the hotspot,
        // is in from the corner by half the difference.
        const int hx = std::max(0, (g_flash.fw - g_sheet.fw) / 2);
        const int hy = std::max(0, (g_flash.fh - g_sheet.fh) / 2);
        for (int i = 0; i < kFlashFrames; i++) {
            flash[i] = make_cursor(g_flash, i, scale, hx, hy);
            ok = ok && (flash[i] != nullptr);
        }
    }
    if (!ok) {
        printf("[SNAP] pointer: the system's stays (%s)\n", SDL_GetError());
        fflush(stdout);
        free_cursors(made, kFrames);
        free_cursors(flash, kFlashFrames);
        return false;
    }
    if (g_flashShown >= 0) {
        SDL_SetCursor(flash[g_flashShown]);
    } else {
        const int frame = (g_frame < 0) ? 0 : g_frame;
        SDL_SetCursor(made[frame]);
        g_frame = frame;
    }
    free_cursors(g_cursor, kFrames);
    free_cursors(g_flashCursor, kFlashFrames);
    for (int i = 0; i < kFrames; i++) {
        g_cursor[i] = made[i];
    }
    for (int i = 0; i < kFlashFrames; i++) {
        g_flashCursor[i] = flash[i];
    }
    g_scale = scale;
    int dw = 1;
    int dh = 1;
    target_size(scale, dw, dh);
    printf("[SNAP] pointer: the port's own, %d by %d at scale %d%s\n", dw, dh, scale,
           (g_flashCursor[0] != nullptr) ? ", with the flash" : "");
    fflush(stdout);
    return true;
}

void show_frame(int frame, const char* why) {
    if (((frame == g_frame) && (g_flashShown < 0)) || (g_cursor[frame] == nullptr)) {
        return;
    }
    SDL_SetCursor(g_cursor[frame]);
    g_frame = frame;
    g_flashShown = -1;
    if (g_test) {
        printf("[SNAP] pointer: frame %d (%s) at %u ms\n", frame, why, unsigned(SDL_GetTicks()));
        fflush(stdout);
    }
}

void show_flash(int i) {
    if ((i == g_flashShown) || (g_flashCursor[i] == nullptr)) {
        return;
    }
    SDL_SetCursor(g_flashCursor[i]);
    g_flashShown = i;
    g_frame = -1;
    if (g_test) {
        printf("[SNAP] pointer: flash %d at %u ms\n", i, unsigned(SDL_GetTicks()));
        fflush(stdout);
    }
}

// A sequence's step: a pointer frame, or a flash frame at kFlashBase and up.
void show_step(int step, const char* why) {
    if (step >= kFlashBase) {
        show_flash(step - kFlashBase);
    } else {
        show_frame(step, why);
    }
}

void start(const int* seq, int len, uint32_t stepMs) {
    g_seq = seq;
    g_seqLen = len;
    g_seqStepMs = stepMs;
    g_seqStartMs = SDL_GetTicks();
}

// The sheets into memory; false, with a line, when the pointer's is absent
// or not four frames in a row. Tried once: a failure leaves the system's
// pointer. The flash's is optional.
bool load_sheet() {
    if (!g_sheet.pixels.empty()) {
        return true;
    }
    if (g_loadFailed) {
        return false;
    }
    if (!load_sheet_file("pointer.png", kFrames, g_sheet)) {
        printf("[SNAP] pointer: the system's stays\n");
        fflush(stdout);
        g_loadFailed = true;
        return false;
    }
    if (!load_sheet_file("pointer_flash.png", kFlashFrames, g_flash)) {
        g_flash = Sheet{};
    }
    return true;
}

// The port's pointer on: the sheets loaded and the cursors made at the
// current scale, the open frame shown. False leaves the system's.
bool turn_on() {
    if (!load_sheet()) {
        return false;
    }
    g_frame = -1;
    g_flashShown = -1;
    g_seq = nullptr;
    if ((g_scale == 0) && !build(pointer_scale())) {
        return false;
    }
    g_frame = 0;
    SDL_SetCursor(g_cursor[0]);
    return true;
}

// The system's pointer back; the cursors are kept for the next turn on.
void turn_off() {
    g_seq = nullptr;
    SDL_SetCursor(SDL_GetDefaultCursor());
}

}  // namespace

void install_pointer(void* sdlWindow) {
    SDL_Window* window = static_cast<SDL_Window*>(sdlWindow);
    if (window == nullptr) {
        return;
    }
    g_test = (getenv("SNAP_POINTER_TEST") != nullptr);
    g_window = window;
    g_want.store(settings().custom_pointer ? 1 : 0);
    if (settings().custom_pointer) {
        g_on = turn_on();
    }
}

void pointer_enable(bool on) {
    g_want.store(on ? 1 : 0);
}

void pointer_click() {
    if (!g_on || (g_cursor[0] == nullptr)) {
        return;
    }
    // A click on a hidden pointer, or on one in its flash out, brings it
    // back the way a move does: the flash in, not the shutter (a click on
    // the hidden pointer showed the shutter and no flash: JackandBeans,
    // Oct 3 2026).
    if ((!g_visible || (g_flashShown >= 0)) && (g_flashCursor[0] != nullptr)) {
        if (g_seq != kFlashIn) {
            start(kFlashIn, int(sizeof(kFlashIn) / sizeof(kFlashIn[0])), uint32_t(kFlashStepMs));
        }
        return;
    }
    start(kClick, int(sizeof(kClick) / sizeof(kClick[0])), kClickStepMs);
}

void pointer_visible(bool visible) {
    if (visible && !g_visible && g_on && (g_cursor[0] != nullptr) && (g_seq != kClick) && (g_seq != kFlashIn)) {
        if (g_flashCursor[0] != nullptr) {
            start(kFlashIn, int(sizeof(kFlashIn) / sizeof(kFlashIn[0])), uint32_t(kFlashStepMs));
        } else {
            start(kAppear, int(sizeof(kAppear) / sizeof(kAppear[0])), kAppearStepMs);
        }
    }
    g_visible = visible;
}

void pointer_hiding_in(int32_t msLeft) {
    g_hideMsLeft = msLeft;
}

void pointer_update() {
    if (g_window == nullptr) {
        return;
    }
    // The Controls page's Pointer row (pointer_enable, from the settings
    // reader) turns the port's pointer on or off; applied here, on the
    // main thread, where SDL's cursor calls belong.
    const bool want = (g_want.load() == 1);
    if (want != g_on) {
        if (want) {
            g_on = turn_on();
            if (!g_on) {
                g_want.store(0);
            }
        } else {
            turn_off();
            g_on = false;
        }
    }
    if (!g_on || (g_cursor[0] == nullptr)) {
        return;
    }
    // The picture's scale can change with the window and with fullscreen. A
    // new scale must hold for twenty pumps (a third of a second) before the
    // cursors are made again, so a wobbling measure cannot flicker them.
    const int scale = pointer_scale();
    if (scale != g_scale) {
        if (scale == g_pendingScale) {
            g_pendingCount++;
        } else {
            g_pendingScale = scale;
            g_pendingCount = 1;
        }
        if (g_pendingCount >= 20) {
            g_pendingCount = 0;
            build(scale);
        }
    } else {
        g_pendingCount = 0;
    }
    const uint32_t now = SDL_GetTicks();
    if (g_test) {
        // Every six seconds: a flash at 4.0 s and a hidden spell to 5.0 s;
        // the self-click keeps clear of them.
        const uint32_t phase = now % 6000u;
        if ((phase >= 4000u) && (phase < 4000u + uint32_t(kFlashMs))) {
            g_hideMsLeft = kFlashMs - int32_t(phase - 4000u);
        } else if ((phase >= 4000u + uint32_t(kFlashMs)) && (phase < 5000u)) {
            g_hideMsLeft = 0;
        } else {
            g_hideMsLeft = INT32_MAX;
        }
        if ((g_seq == nullptr) && ((now - g_testAtMs) >= 1500u) && ((phase < 3500u) || (phase >= 5500u))) {
            g_testAtMs = now;
            pointer_click();
        }
    }
    // The flash as the pointer leaves: in the last four tenths of a second
    // before fullscreen hides it, the burst's frames. Once hidden, the
    // cursor rests on the last of them, the flash in's first, so the
    // instant the system shows the cursor again it is already the burst
    // (resting on the open lens gave one refresh of the plain pointer
    // before the burst: the "glitched frame" JackandBeans saw, Oct 3 2026).
    if ((g_flashCursor[0] != nullptr) && (g_hideMsLeft <= kFlashMs)) {
        g_seq = nullptr;
        const int i = (g_hideMsLeft > 0)
            ? std::clamp(int((kFlashMs - g_hideMsLeft) / kFlashStepMs), 0, kFlashFrames - 1)
            : (kFlashFrames - 1);
        show_flash(i);
        return;
    }
    // A flash out cut short by the hand moving: back from the frame it
    // reached to the pointer, through the flash in's frames (the burst
    // stayed on the moving cursor before: JackandBeans, Oct 3 2026).
    if ((g_flashShown >= 0) && (g_seq == nullptr)) {
        start(kFlashIn, int(sizeof(kFlashIn) / sizeof(kFlashIn[0])), uint32_t(kFlashStepMs));
        g_seqStartMs = now - uint32_t(kFlashFrames - 1 - g_flashShown) * uint32_t(kFlashStepMs);
    }
    if (g_seq == nullptr) {
        return;
    }
    const uint32_t step = (now - g_seqStartMs) / g_seqStepMs;
    const char* why = (g_seq == kClick) ? "click" : ((g_seq == kFlashIn) ? "flash in" : "appear");
    if (step >= uint32_t(g_seqLen)) {
        g_seq = nullptr;
        show_frame(0, why);
        return;
    }
    show_step(g_seq[step], why);
}

}  // namespace snap
