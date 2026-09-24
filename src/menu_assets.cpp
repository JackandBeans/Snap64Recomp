/**
 * @file menu_assets.cpp
 * @brief Stages the GRAPHICS menu's text and carries its settings mailbox.
 *
 * The in-game GRAPHICS page (patches/src/graphics_menu_patch.c) draws with
 * the game's own UI font, but the font's code lives in the window overlay,
 * which the main menu unloads -- so the glyphs have to arrive as pixels, not
 * as calls. The glyphs are the interface font's own pre-rendered sprites,
 * cut out of RDRAM by src/menu_harvest.cpp the moment the game has
 * decompressed the main menu's VPK0 segment (the dmaReadVPK0 wrapper in
 * src/overlay_hook.cpp); the repository carries none of them. This file
 * composites every string the page needs into an IA16 strip with the
 * interface's own two-pixel drop shadow, and writes the strips into
 * otherwise-unused RDRAM where the patch wraps them in sprites.
 *
 * Settings cross the same boundary through a mailbox at 0x80C00000 (a magic
 * word, a sequence counter, one byte per graphics setting, and a second
 * bank for sound): this side
 * seeds it with the saved values, the page edits bytes and bumps a sequence
 * counter, and the poll below applies on each bump -- so every change takes
 * effect while the menu is still open, through exactly the same path the
 * hotkeys use -- and marks the settings dirty. The disk write is the main
 * thread's, debounced (settings_flush_if_due in settings.h), never this
 * thread's.
 *
 * All RDRAM writes go through the recompiler's addressing (words direct,
 * halves XOR 2, bytes XOR 3), which also matches what a DMA from ROM would
 * have produced -- the RDP reads these strips exactly as it reads any other
 * texture the game loaded.
 */
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "audio.h"
#include "hle/rt64_snap_diag.h"
#include "input.h"
#include "librecomp/game.hpp"
#include "librecomp/mods.hpp"
#include "recomp.h"

#include "paths.h"
#include "settings.h"
#include "snap_station.h"
#include "version.h"
#include "vr/vr_service.h"

// stb_image's implementation is compiled inside the RT64 library
// (rt64_texture_cache.cpp); this include only brings the declarations.
#include "stb/stb_image.h"

#include <SDL.h>

// stb_image hands a narrow file name to fopen, which on Windows is the ANSI
// code page: an install path with a character outside it would lose the
// badge and every override. Open the file here, hand stb the stream.
static stbi_uc* load_png(const std::filesystem::path& path, int* w, int* h, int* comp) {
#if defined(_WIN32)
    FILE* f = _wfopen(path.c_str(), L"rb");
#else
    FILE* f = fopen(path.c_str(), "rb");
#endif
    if (f == nullptr) {
        return nullptr;
    }
    stbi_uc* data = stbi_load_from_file(f, w, h, comp, 4);
    fclose(f);
    return data;
}

namespace snap {

// The window's icon, from Snap64Recomp-window.png beside the executable:
// the film canister alone, which is what the Windows title bar shows from
// the .ico's small entries (SDL takes the executable's resource there
// without being asked). A Linux window has one image for every size, and
// the whole logo on its tile is a purple square at 16 px, so the canister
// it is. A Linux binary has no icon of its own, so the Deck's window and
// taskbar showed nothing until this. False when the file is absent, which
// is the Windows archive, where the resource already serves.
bool set_window_icon(void* sdlWindow) {
    int w = 0;
    int h = 0;
    int comp = 0;
    stbi_uc* data = load_png(exe_path("Snap64Recomp-window.png"), &w, &h, &comp);
    if (data == nullptr) {
        return false;
    }
    SDL_Surface* icon = SDL_CreateRGBSurfaceWithFormatFrom(data, w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
    if (icon != nullptr) {
        SDL_SetWindowIcon(static_cast<SDL_Window*>(sdlWindow), icon);
        SDL_FreeSurface(icon);
    }
    stbi_image_free(data);
    return icon != nullptr;
}

// A file the port ships beside the executable, or the player's own copy in
// the data directory when that is a different folder (a read-only Linux
// install; paths.h): the player's wins. On Windows and on a writable
// install the two folders are one, and this is base_path.
static std::filesystem::path asset_path(const std::string& rel) {
    const std::filesystem::path own = base_path(rel);
    if (base_dir() == exe_dir()) {
        return own;
    }
    std::error_code ec;
    if (std::filesystem::exists(own, ec)) {
        return own;
    }
    return exe_path(rel);
}

// A launcher written once on Linux if none is there: beside the executable
// when that folder can be written, else in the data directory (paths.h).
// A Linux binary cannot carry an icon the way a Windows one does; what a
// file manager or an application menu shows is a .desktop entry, and this
// is one, with the absolute paths such an entry needs -- the executable,
// its folder and the tile beside it, wherever the entry itself went: run
// it, or copy it to ~/.local/share/applications for the menu. Nothing is
// written when a file of that name exists, so an edited one stays as
// edited.
void write_desktop_entry() {
    const std::filesystem::path entry = base_path("Snap64Recomp.desktop");
    std::error_code ec;
    if (std::filesystem::exists(entry, ec)) {
        return;
    }
    const std::filesystem::path dir = exe_path("");
    const std::filesystem::path exe = exe_path("Snap64Recomp");
    const std::filesystem::path icon = exe_path("Snap64Recomp.png");
    FILE* f = fopen(entry.string().c_str(), "wb");
    if (f == nullptr) {
        return;
    }
    fprintf(f,
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Snap64 Recomp\n"
        "Comment=Pokemon Snap, running natively\n"
        "Exec=\"%s\"\n"
        "Path=%s\n"
        "Icon=%s\n"
        "Terminal=false\n"
        "Categories=Game;\n",
        exe.string().c_str(), dir.string().c_str(), icon.string().c_str());
    fclose(f);
    printf("[SNAP] wrote %s (a launcher with the port's icon; copy it to ~/.local/share/applications for the menu)" "\n",
           entry.string().c_str());
}

}

// The menu sprite fonts and furniture, harvested from RDRAM at run time.
#include "menu_harvest.h"

namespace snap {

namespace {

constexpr uint32_t MailboxAddr = 0x80C00000u;
constexpr uint32_t DirectoryAddr = 0x80C01000u;
// The directory holds an 8-byte header plus 8 bytes per string id; the
// pixel cursor must start beyond the LAST entry, not at a round number.
// At 0x...1100 the entries for ids 31+ silently overwrote the first
// staged tile's pixels (invisibly -- address bytes decode as near-black
// texels on the black backdrop). 0x1000 of directory seats 511 ids; the
// 0x800 it had seated 255, and the Mods page's 260 would have put five
// entries into the first tile; the 0x400 before that seated 127, and the
// 129 strings of 1.0.5 had already put two entries into the black tile's
// pixels, which nothing drew. The patch reads each entry's own address, so
// only this constant moves.
constexpr uint32_t PixelsAddr = 0x80C02000u;
constexpr uint32_t MailboxMagic = 0x53474658u;   // 'SGFX'
constexpr uint32_t DirectoryMagic = 0x53474130u; // 'SGA0'
constexpr uint32_t STR_ITEM_LABEL_ID = 1;        // "Graphics", the Option item

// The strips are exactly as tall as the original menu sprites: ten rows.
constexpr int StripHeight = kMenuFontCellH;

// The BUTTON SETUP page's row values (ids kBindDynBase on, two banks of
// eighteen) are composed while the page is open, for the device it shows,
// so their pixels have a fixed home of their own: well past the staged
// strings, which end near 0x80CA0000, and below librecomp's mod space at
// 0x81000000. Three chunks a strip, 192 pixels, the widest a value gets.
constexpr uint32_t DynPixelsAddr = 0x80D00000u;
constexpr int DynChunks = 3;
constexpr uint32_t DynStripBytes = uint32_t(DynChunks * 64 * StripHeight * 2);
constexpr uint32_t kStringBaseCount = 30;   // the strings[] table below, asserted there
constexpr uint32_t kBindDynBase = kStringBaseCount + 162;   // graphics_menu_patch.c STR_BIND_DYN
constexpr int BindInputCount = 18;

// The Mods page's rows (ids kModsDynBase on): two banks of six names and six
// help lines, composed for the window the page shows while it is open, in
// the same fixed home after the Button Setup page's slots. A name is a body
// strip like a row value; a help line is two lines tall (compose_lines) and
// up to four chunks wide, the help box's width, so its slots are bigger.
constexpr uint32_t kModsDynBase = kStringBaseCount + 206;   // graphics_menu_patch.c STR_MODS_DYN
constexpr int ModsVisible = 6;
constexpr int ModsBank = 2 * ModsVisible;                   // ids per bank: names, then help lines
constexpr int ModsHelpChunks = 4;
constexpr int ModsHelpHeight = 12 + kMenuHlpCellH;          // compose_lines' height
constexpr uint32_t ModsHelpBytes = uint32_t(ModsHelpChunks * 64 * ModsHelpHeight * 2);
constexpr uint32_t ModsNamesAddr = DynPixelsAddr + uint32_t(2 * BindInputCount) * DynStripBytes;
constexpr uint32_t ModsHelpsAddr = ModsNamesAddr + uint32_t(2 * ModsVisible) * DynStripBytes;
constexpr int ModsNameInkWidth = 156;                       // the label column, before the page's values at x=212
constexpr int ModsHelpInkWidth = 236;                       // the help box's text width (the stock sentences reach 238)
// Where a Mods page id's pixels live: names in one run of slots, help lines
// in another, both banks side by side.
static uint32_t mods_dyn_addr(uint32_t id) {
    const uint32_t k = id - kModsDynBase;
    const uint32_t bank = k / uint32_t(ModsBank);
    const uint32_t slot = k % uint32_t(ModsBank);
    if (slot < uint32_t(ModsVisible)) {
        return ModsNamesAddr + (bank * uint32_t(ModsVisible) + slot) * DynStripBytes;
    }
    return ModsHelpsAddr + (bank * uint32_t(ModsVisible) + (slot - uint32_t(ModsVisible))) * ModsHelpBytes;
}

struct Strip {
    int width = 0;
    int height = StripHeight;
    std::vector<uint8_t> intensity;  // width * height
    std::vector<uint8_t> alpha;
};

uint8_t* g_menu_rdram = nullptr;

// The harvested faces and furniture; empty until the first main-menu load.
MenuFont g_font;

// Characters a staged string asked for that no face carries. Counted while
// compositing so a harvest that segmented differently from the reference
// withholds the directory instead of putting a string with holes on screen.
int g_missing_glyphs = 0;

void note_missing(char c) {
    g_missing_glyphs++;
    printf("[SNAP-MENU] no glyph for '%c' (0x%02X) in the harvested faces\n",
           (c >= 0x20) ? c : '?', unsigned(uint8_t(c)));
}

void write_u32(uint32_t addr, uint32_t v) {
    *reinterpret_cast<uint32_t*>(g_menu_rdram + (addr - 0x80000000u)) = v;
}

uint32_t read_u32_mail(uint32_t addr) {
    return *reinterpret_cast<uint32_t*>(g_menu_rdram + (addr - 0x80000000u));
}

void write_u16(uint32_t addr, uint16_t v) {
    *reinterpret_cast<uint16_t*>(g_menu_rdram + ((addr ^ 2u) - 0x80000000u)) = v;
}

void write_u8(uint32_t addr, uint8_t v) {
    g_menu_rdram[(addr ^ 3u) - 0x80000000u] = v;
}

uint8_t read_u8_mail(uint32_t addr) {
    return g_menu_rdram[(addr ^ 3u) - 0x80000000u];
}

const MenuGlyph* menu_glyph(char c) {
    return g_font.body.find(c);
}

// Composites a string from the menu sprite font: the original letterforms,
// each cell blitted as decoded (no synthetic shadow -- the originals bake
// none), advanced by ink width plus the gap measured from the original
// layouts. The cursor tracks ink columns; each cell carries one column of
// antialiasing fringe on either side.
Strip compose(const char* text, bool lenient = false) {
    Strip strip;

    int xc = 1;
    for (const char* c = text; *c != 0; c++) {
        if (*c == ' ') {
            xc += kMenuFontSpaceGap;
        }
        else if (*c == '<') {
            xc += g_font.brkL.w - 2;
        }
        else if (*c == '>') {
            xc += g_font.brkR.w - 2;
        }
        else if (const MenuGlyph* g = menu_glyph(*c)) {
            xc += g->coreW + kMenuFontLetterGap;
        }
        else {
            xc += 6;
        }
    }

    // Rounded up to whole 64-texel blocks: each block is loaded with a
    // single block load, and the loader's odd-row swap only lines up when a
    // row is a multiple of eight bytes. A narrower final block shears into
    // diagonals -- measured on screen before this rounding existed.
    int width = (xc + 2 + 63) & ~63;

    strip.width = width;
    strip.intensity.assign(size_t(width) * StripHeight, 0);
    strip.alpha.assign(size_t(width) * StripHeight, 0);

    auto blend = [&](int px, int py, uint8_t i, uint8_t a) {
        if ((px < 0) || (px >= width) || (py < 0) || (py >= StripHeight) || (a == 0)) {
            return;
        }
        const size_t at = size_t(py) * width + px;
        if (a >= strip.alpha[at]) {
            strip.intensity[at] = std::max(strip.intensity[at], i);
            strip.alpha[at] = a;
        }
    };

    xc = 1;
    for (const char* c = text; *c != 0; c++) {
        if (*c == ' ') {
            xc += kMenuFontSpaceGap;
            continue;
        }
        if ((*c == '<') || (*c == '>')) {
            // The original chevrons, as decoded pixels.
            const bool left = (*c == '<');
            const MenuBitmap& brk = left ? g_font.brkL : g_font.brkR;
            const int bw = brk.w;
            const int bh = brk.h;
            const unsigned char* ia = brk.ia.data();
            for (int gy = 0; (gy < bh) && (gy < StripHeight); gy++) {
                for (int gx = 0; gx < bw; gx++) {
                    blend(xc - 1 + gx, gy, ia[(gy * bw + gx) * 2 + 0], ia[(gy * bw + gx) * 2 + 1]);
                }
            }
            xc += bw - 2;
            continue;
        }
        const MenuGlyph* g = menu_glyph(*c);
        if (g == nullptr) {
            if (lenient) {
                // A key named with a character no face carries (the
                // Button Setup page's live values): a dim block stands in, and
                // the directory is not withheld for it.
                for (int gy = 4; gy < 9; gy++) {
                    for (int gx = 0; gx < 4; gx++) {
                        blend(xc + gx, gy, 255, 96);
                    }
                }
                xc += 6;
                continue;
            }
            note_missing(*c);
            xc += 6;
            continue;
        }
        const unsigned char* ia = g_font.body.ia.data() + size_t(g->off) * 2;
        for (int gy = 0; gy < kMenuFontCellH; gy++) {
            for (int gx = 0; gx < g->cellW; gx++) {
                blend(xc - g->coreStart + gx, gy,
                      ia[(gy * g->cellW + gx) * 2 + 0], ia[(gy * g->cellW + gx) * 2 + 1]);
            }
        }
        xc += g->coreW + kMenuFontLetterGap;
    }

    return strip;
}

// A row value of the Button Setup page: any key's name, in the body face, cut
// to what three chunks hold.
Strip compose_dyn(std::string text) {
    Strip strip = compose(text.c_str(), true);
    while ((strip.width > DynChunks * 64) && !text.empty()) {
        text.pop_back();
        strip = compose(text.c_str(), true);
    }
    return strip;
}


// One title-menu item in the title's own face: the large ringed letters of
// "New Game", "Continue", "Options" and "Gallery" (menu_harvest.cpp). Cores
// one column apart and six across a space, as measured on the originals; a
// core pixel wins over ring and ring over fringe, so the shared ring columns
// between letters read as they do in the sprites. The text is centred in
// its 64-texel-rounded width, so the patch places the strip by its centre.
// A letter the face lacks leaves the strip empty (width 0) and is said, but
// does not withhold the directory: this is the one string that may be
// absent, and the patch then keeps the stock four items.
Strip compose_title(const char* text) {
    Strip strip;
    strip.height = kMenuTtlCellH;
    const MenuFace& face = g_font.ttl;

    int ink = 0;
    for (const char* c = text; *c != 0; c++) {
        if (*c == ' ') {
            ink += kMenuTtlSpaceGap;
            continue;
        }
        const MenuGlyph* g = face.find(*c);
        if (g == nullptr) {
            printf("[SNAP-MENU] the title face has no '%c'; the title's Snap Station entry is left out\n", *c);
            strip.width = 0;
            return strip;
        }
        ink += g->coreW + kMenuTtlLetterGap;
    }
    ink -= kMenuTtlLetterGap;

    // Room for the face's shadow (five pixels) either side of the ink.
    const int width = (ink + 12 + 63) & ~63;
    strip.width = width;
    strip.intensity.assign(size_t(width) * strip.height, 0);
    strip.alpha.assign(size_t(width) * strip.height, 0);

    auto blend = [&](int px, int py, uint8_t i, uint8_t a) {
        if ((px < 0) || (px >= width) || (py < 0) || (py >= strip.height) || (a == 0)) {
            return;
        }
        const size_t at = size_t(py) * width + px;
        const bool newCore = (a >= 128) && (i >= 128);
        const bool oldCore = (strip.alpha[at] >= 128) && (strip.intensity[at] >= 128);
        if (newCore) {
            strip.intensity[at] = i;
            strip.alpha[at] = a;
            return;
        }
        if (oldCore) {
            return;
        }
        if (a > strip.alpha[at]) {
            strip.intensity[at] = i;
            strip.alpha[at] = a;
        }
    };

    int xc = (width - ink) / 2;
    for (const char* c = text; *c != 0; c++) {
        if (*c == ' ') {
            xc += kMenuTtlSpaceGap;
            continue;
        }
        const MenuGlyph* g = face.find(*c);
        const unsigned char* ia = face.ia.data() + size_t(g->off) * 2;
        for (int gy = 0; gy < kMenuTtlCellH; gy++) {
            for (int gx = 0; gx < g->cellW; gx++) {
                blend(xc - g->coreStart + gx, gy,
                      ia[(gy * g->cellW + gx) * 2 + 0], ia[(gy * g->cellW + gx) * 2 + 1]);
            }
        }
        xc += g->coreW + kMenuTtlLetterGap;
    }

    // The face's shadow around the whole word. A cell keeps one column
    // either side of its core, so the composed word had a one-pixel ring
    // where the game's word sprites carry a shadow five pixels wide; every
    // pixel that is not core takes at least the measured falloff of its
    // distance to the nearest core pixel (title_shadow_alpha).
    {
        std::vector<std::pair<int, int>> cores;
        for (int y = 0; y < strip.height; y++) {
            for (int x = 0; x < width; x++) {
                const size_t at = size_t(y) * width + x;
                if ((strip.alpha[at] >= 128) && (strip.intensity[at] >= 128)) {
                    cores.emplace_back(x, y);
                }
            }
        }
        for (int y = 0; y < strip.height; y++) {
            for (int x = 0; x < width; x++) {
                const size_t at = size_t(y) * width + x;
                if ((strip.alpha[at] >= 128) && (strip.intensity[at] >= 128)) {
                    continue;
                }
                float best = 1.0e9f;
                for (const auto& c : cores) {
                    const float dx = float(c.first - x);
                    const float dy = float(c.second - y);
                    best = std::min(best, dx * dx + dy * dy);
                }
                if (best > 36.0f) {
                    continue;
                }
                const uint8_t shadow = title_shadow_alpha(std::sqrt(best));
                if (shadow > strip.alpha[at]) {
                    strip.alpha[at] = shadow;
                    strip.intensity[at] = 0;
                }
            }
        }
    }
    return strip;
}

// One line in the header face -- the medium font of the Options screen's
// own title sprite, for the page's "Graphics" heading.
Strip compose_hdr(const char* text) {
    Strip strip;
    strip.height = kMenuHdrCellH;

    auto hdr_glyph = [](char c) -> const MenuGlyph* { return g_font.hdr.find(c); };

    int xc = 1;
    for (const char* c = text; *c != 0; c++) {
        if (*c == ' ') {
            xc += kMenuFontSpaceGap + 2;
        }
        else if (const MenuGlyph* g = hdr_glyph(*c)) {
            xc += g->coreW + kMenuFontLetterGap;
        }
        else {
            xc += 6;
        }
    }
    strip.width = (xc + 2 + 63) & ~63;
    strip.intensity.assign(size_t(strip.width) * strip.height, 0);
    strip.alpha.assign(size_t(strip.width) * strip.height, 0);

    xc = 1;
    for (const char* c = text; *c != 0; c++) {
        if (*c == ' ') {
            xc += kMenuFontSpaceGap + 2;
            continue;
        }
        const MenuGlyph* g = hdr_glyph(*c);
        if (g == nullptr) {
            note_missing(*c);
            xc += 6;
            continue;
        }
        const unsigned char* ia = g_font.hdr.ia.data() + size_t(g->off) * 2;
        for (int gy = 0; gy < kMenuHdrCellH; gy++) {
            for (int gx = 0; gx < g->cellW; gx++) {
                const int px = xc - g->coreStart + gx;
                if ((px < 0) || (px >= strip.width)) {
                    continue;
                }
                const uint8_t i = ia[(gy * g->cellW + gx) * 2 + 0];
                const uint8_t a = ia[(gy * g->cellW + gx) * 2 + 1];
                const size_t at = size_t(gy) * strip.width + px;
                if ((a != 0) && (a >= strip.alpha[at])) {
                    strip.intensity[at] = std::max(strip.intensity[at], i);
                    strip.alpha[at] = a;
                }
            }
        }
        xc += g->coreW + kMenuFontLetterGap;
    }
    return strip;
}

// One line in the help face -- the antialiased rendering of the menu
// letterforms that the stock help sentences use. The labels are hard-edged
// and the help box is soft; each face keeps to its own rooms. The stock
// sentences were laid out with per-pair kerning ("ay" snugs to one pixel
// where "sp" takes three), so the gap between two glyphs comes from the
// pair table harvested off those sentences, with the face's default gap
// covering pairs the stock text never set.
Strip compose_help(const char* text) {
    Strip strip;
    strip.height = kMenuHlpCellH;

    auto hlp_glyph = [](char c) -> const MenuGlyph* { return g_font.hlp.find(c); };
    auto kern = [](char a, char b, bool spaced) -> int {
        const std::vector<MenuKern>& table = spaced ? g_font.hlpSpaceKern : g_font.hlpKern;
        for (const MenuKern& k : table) {
            if ((k.a == a) && (k.b == b)) {
                return k.gap;
            }
        }
        return spaced ? kMenuHlpSpaceGap : kMenuHlpLetterGap;
    };

    // One walk computes the layout; the second paints it. A character
    // outside the face still takes room: a silent zero advance would fuse
    // its neighbours with nothing to show a string edit went too far.
    bool counting = false;   // the missing-glyph count is taken on the painting walk only
    auto walk = [&](auto&& place) {
        int xc = 1;
        char prev = 0;
        bool started = false;
        bool spaced = false;
        for (const char* c = text; *c != 0; c++) {
            if (*c == ' ') {
                spaced = true;
                continue;
            }
            const MenuGlyph* g = hlp_glyph(*c);
            if (started) {
                xc += (prev != 0) ? kern(prev, *c, spaced)
                                  : (spaced ? kMenuHlpSpaceGap : kMenuHlpLetterGap);
            }
            started = true;
            if (g == nullptr) {
                if (counting) {
                    note_missing(*c);
                }
                xc += 6;
                prev = 0;
                spaced = false;
                continue;
            }
            place(g, xc);
            xc += g->coreW;
            prev = *c;
            spaced = false;
        }
        return xc;
    };

    const int visW = walk([](const MenuGlyph*, int) {});
    strip.width = (visW + 2 + 63) & ~63;
    strip.intensity.assign(size_t(strip.width) * strip.height, 0);
    strip.alpha.assign(size_t(strip.width) * strip.height, 0);

    counting = true;
    walk([&](const MenuGlyph* g, int xc) {
        const unsigned char* ia = g_font.hlp.ia.data() + size_t(g->off) * 2;
        for (int gy = 0; gy < kMenuHlpCellH; gy++) {
            for (int gx = 0; gx < g->cellW; gx++) {
                const int px = xc - g->coreStart + gx;
                if ((px < 0) || (px >= strip.width)) {
                    continue;
                }
                const uint8_t i = ia[(gy * g->cellW + gx) * 2 + 0];
                const uint8_t a = ia[(gy * g->cellW + gx) * 2 + 1];
                const size_t at = size_t(gy) * strip.width + px;
                if ((a != 0) && (a >= strip.alpha[at])) {
                    strip.intensity[at] = std::max(strip.intensity[at], i);
                    strip.alpha[at] = a;
                }
            }
        }
    });
    return strip;
}

void apply_outline(Strip &strip);

// The scroll arrows: the values' own big chevron turned on its side to
// point up or down, so the Graphics page can say that more settings sit
// off-screen. Same pixels the player already reads as "more this way",
// at the chevrons' own 1:1 scale -- an indicator must not outrank the
// controls it serves -- wearing the credits line's treatment: rainbow
// cores in a baked black ring, recoloured live by the same animator.
// One row and column of padding so the ring fits around the chevron.
Strip compose_scroll_arrow(bool up) {
    Strip strip;
    const MenuBitmap& brk = g_font.brkL;
    strip.height = brk.w + 2;   // rotated: width becomes height, plus ring
    strip.width = 64;
    strip.intensity.assign(size_t(strip.width) * strip.height, 0);
    strip.alpha.assign(size_t(strip.width) * strip.height, 0);
    for (int y = 0; y < brk.h; y++) {
        for (int x = 0; x < brk.w; x++) {
            const uint8_t a = brk.ia[(y * brk.w + x) * 2 + 1];
            // '<' points left; a quarter turn clockwise points it up, and
            // the down arrow is that up arrow's exact vertical mirror --
            // twins by construction, where opposite rotations would each
            // inherit a different side of the source glyph's asymmetry.
            // The chevron's antialiasing was authored for horizontal
            // reading and the RGBA16 staging can only keep or drop a
            // texel, so the strong fringe joins the silhouette outright:
            // full symmetric limbs instead of edges nibbled differently
            // on each side.
            const int rx = 1 + (brk.h - 1 - y);
            const int ry = 1 + (up ? x : (brk.w - 1 - x));
            if (a >= 96) {
                strip.intensity[size_t(ry) * strip.width + rx] = 255;
                strip.alpha[size_t(ry) * strip.width + rx] = 255;
            }
        }
    }
    apply_outline(strip);
    return strip;
}

// Two help-face lines stacked at the stock help sprites' own line pitch of
// twelve rows -- the settings descriptions in the help box.
Strip compose_lines(const char* line1, const char* line2) {
    Strip a = compose_help(line1);
    Strip b = compose_help(line2);
    Strip out;
    out.height = 12 + kMenuHlpCellH;
    out.width = std::max(a.width, b.width);
    out.intensity.assign(size_t(out.width) * out.height, 0);
    out.alpha.assign(size_t(out.width) * out.height, 0);
    for (int y = 0; y < kMenuHlpCellH; y++) {
        for (int x = 0; x < a.width; x++) {
            const size_t dst = size_t(y) * out.width + x;
            const size_t src = size_t(y) * a.width + x;
            out.intensity[dst] = a.intensity[src];
            out.alpha[dst] = a.alpha[src];
        }
        if (y + 12 < out.height) {
            for (int x = 0; x < b.width; x++) {
                const size_t dst = size_t(y + 12) * out.width + x;
                const size_t src = size_t(y) * b.width + x;
                out.intensity[dst] = std::max(out.intensity[dst], b.intensity[src]);
                out.alpha[dst] = std::max(out.alpha[dst], b.alpha[src]);
            }
        }
    }
    return out;
}

// The bold black border of the title screen's copyright block: one pixel
// of opaque black in every empty 8-neighbourhood cell around a glyph core.
// That block bakes its ring into RGBA16 texel data (0x0001 black around
// 0xFFFF white -- decoded straight from the ROM sprite), so the credits
// line bakes the identical ring. Menu text gets none of this: the stock
// IA sprites are plain white whose only edge is their antialiased fringe,
// and the draw path has no border facility at all.
void apply_outline(Strip &strip) {
    const std::vector<uint8_t> coreA = strip.alpha;
    const std::vector<uint8_t> coreI = strip.intensity;
    for (int y = 0; y < strip.height; y++) {
        for (int x = 0; x < strip.width; x++) {
            const size_t at = size_t(y) * strip.width + x;
            if ((coreA[at] >= 128) && (coreI[at] >= 128)) {
                continue;   // a core pixel stays a core pixel
            }
            bool edge = false;
            for (int dy = -1; dy <= 1 && !edge; dy++) {
                for (int dx = -1; dx <= 1 && !edge; dx++) {
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if ((nx < 0) || (nx >= strip.width) || (ny < 0) || (ny >= strip.height)) {
                        continue;
                    }
                    const size_t nat = size_t(ny) * strip.width + nx;
                    edge = (coreA[nat] >= 128) && (coreI[nat] >= 128);
                }
            }
            if (edge) {
                strip.intensity[at] = 0;
                strip.alpha[at] = 255;
            }
        }
    }
}

// One line in the credits face -- the condensed 1px font of the title
// screen's copyright block, for the port's own line beneath it.
Strip compose_credits(const char* text) {
    Strip strip;

    auto crd_glyph = [](char c) -> const MenuGlyph* { return g_font.crd.find(c); };

    // The copyright's own tracking: one pixel between glyphs, borders
    // fusing across the gap exactly as the stock lines above fuse.
    // Punctuation the stock face never sets -- parens, the ampersand,
    // the middle dot -- takes extra air, and the word spaces run a pixel
    // wide, which lands the release line (`... v1.0.0`) at 168px against
    // the first stock line's measured 169: the equal length the layout
    // asks for. A prerelease tag (version.h.in) adds its own glyphs to
    // that.
    auto roomy = [](char c) {
        return (c == '(') || (c == ')') || (c == '&') || (c == '\x01');
    };
    auto gap_before = [&roomy](char prev, char cur) {
        if (prev == 0) {
            return 0;
        }
        return (roomy(prev) || roomy(cur)) ? 3 : 1;
    };

    int xc = 1;
    char prev = 0;
    for (const char* c = text; *c != 0; c++) {
        if (*c == ' ') {
            xc += 6;
            prev = 0;
        }
        else if (const MenuGlyph* g = crd_glyph(*c)) {
            xc += gap_before(prev, *c) + g->coreW;
            prev = *c;
        }
    }
    const int visW = xc + 1;
    strip.width = (visW + 63) & ~63;
    strip.intensity.assign(size_t(strip.width) * StripHeight, 0);
    strip.alpha.assign(size_t(strip.width) * StripHeight, 0);

    // Centred inside the padded buffer, so placing the strip at
    // 160 - width/2 centres the visible text on screen.
    xc = 1 + (strip.width - visW) / 2;
    prev = 0;
    for (const char* c = text; *c != 0; c++) {
        if (*c == ' ') {
            xc += 6;
            prev = 0;
            continue;
        }
        const MenuGlyph* g = crd_glyph(*c);
        if (g == nullptr) {
            // The face is the copyright block's (menu_harvest.cpp): no '-',
            // no '2', '3' or '7'. A version string that needs one of them
            // (version.h.in) is not quietly clipped: note_missing() withholds
            // every staged string, so the miss is loud.
            note_missing(*c);
            continue;
        }
        xc += gap_before(prev, *c);
        prev = *c;
        const unsigned char* ia = g_font.crd.ia.data() + size_t(g->off) * 2;
        for (int gy = 0; gy < kMenuFontCellH; gy++) {
            for (int gx = 0; gx < g->cellW; gx++) {
                const int px = xc - g->coreStart + gx;
                if ((px < 0) || (px >= strip.width)) {
                    continue;
                }
                const uint8_t i = ia[(gy * g->cellW + gx) * 2 + 0];
                const uint8_t a = ia[(gy * g->cellW + gx) * 2 + 1];
                const size_t at = size_t(gy) * strip.width + px;
                if ((a != 0) && (a >= strip.alpha[at])) {
                    strip.intensity[at] = std::max(strip.intensity[at], i);
                    strip.alpha[at] = a;
                }
            }
        }
        xc += g->coreW;
    }
    return strip;
}

// Prepends the original bullet dot to a composed label strip, at the same
// spacing the stock Option items use: dot pixels first, the text starting
// at the column the original "Screen" label starts its S.
Strip add_item_dot(const Strip &text) {
    Strip out;
    const int textStart = g_font.dotTextStart;
    const MenuBitmap& dot = g_font.dot;
    out.width = (textStart + text.width + 63) & ~63;
    out.intensity.assign(size_t(out.width) * StripHeight, 0);
    out.alpha.assign(size_t(out.width) * StripHeight, 0);
    for (int y = 0; y < StripHeight; y++) {
        for (int x = 0; x < text.width; x++) {
            out.intensity[size_t(y) * out.width + textStart + x] = text.intensity[size_t(y) * text.width + x];
            out.alpha[size_t(y) * out.width + textStart + x] = text.alpha[size_t(y) * text.width + x];
        }
    }
    for (int y = 0; y < dot.h && y < StripHeight; y++) {
        for (int x = 0; x < dot.w; x++) {
            const uint8_t di = dot.ia[(y * dot.w + x) * 2 + 0];
            const uint8_t da = dot.ia[(y * dot.w + x) * 2 + 1];
            if (da != 0) {
                out.intensity[size_t(y) * out.width + x] = di;
                out.alpha[size_t(y) * out.width + x] = da;
            }
        }
    }
    return out;
}

// The interface's thin +1,+1 drop shadow, rebuilt from a strip's alpha mask
// exactly the way compose() lays it under the glyphs. Used for overrides
// whose source image had no transparency, where any authored shadow was
// indistinguishable from the background and lost in conversion.
void apply_shadow(Strip &strip) {
    const std::vector<uint8_t> srcI = strip.intensity;
    const std::vector<uint8_t> srcA = strip.alpha;
    std::fill(strip.intensity.begin(), strip.intensity.end(), 0);
    std::fill(strip.alpha.begin(), strip.alpha.end(), 0);

    auto blend = [&](int px, int py, uint8_t i, uint8_t a) {
        if ((px < 0) || (px >= strip.width) || (py < 0) || (py >= StripHeight) || (a == 0)) {
            return;
        }
        const size_t at = size_t(py) * strip.width + px;
        if (a >= strip.alpha[at]) {
            strip.intensity[at] = std::max(strip.intensity[at], i);
            strip.alpha[at] = a;
        }
    };

    for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < StripHeight; y++) {
            for (int x = 0; x < strip.width; x++) {
                const size_t at = size_t(y) * strip.width + x;
                if (srcA[at] == 0) {
                    continue;
                }
                if (pass == 0) {
                    blend(x + 1, y + 1, 0, srcA[at]);
                }
                else {
                    blend(x, y, srcI[at], srcA[at]);
                }
            }
        }
    }
}

// A hand-made replacement for one string: menu_text/<name>.png next to the
// executable. Any resolution; downscaled to the strip height with a box
// filter. A transparent background is ideal (alpha carried through,
// luminance becoming the glyph intensity), but a fully opaque image --
// image generators love white-on-black -- converts automatically: luminance
// becomes the alpha, the glyphs read as white, and the interface's drop
// shadow is rebuilt underneath. Missing or unreadable files simply fall
// back to the font renderer, so partial sets are fine.
bool load_override(const char* name, Strip &strip) {
    int w = 0, h = 0, comp = 0;
    stbi_uc* data = load_png(asset_path("menu_text/" + std::string(name) + ".png"), &w, &h, &comp);
    if (data == nullptr) {
        return false;
    }
    if ((w <= 0) || (h <= 0)) {
        stbi_image_free(data);
        return false;
    }

    // A fully opaque image can carry no transparency information: treat its
    // luminance as the alpha channel instead, so white-on-black renders as
    // white-on-transparent.
    bool opaque = true;
    for (int i = 0; i < w * h; i++) {
        if (data[size_t(i) * 4 + 3] < 250) {
            opaque = false;
            break;
        }
    }

    // Scale to the strip height, preserving aspect.
    const double scale = double(StripHeight) / double(h);
    int outW = std::max(1, int(w * scale + 0.5));
    outW = (outW + 63) & ~63;
    strip.width = outW;
    strip.intensity.assign(size_t(outW) * StripHeight, 0);
    strip.alpha.assign(size_t(outW) * StripHeight, 0);

    for (int y = 0; y < StripHeight; y++) {
        const int sy0 = int(y / scale);
        const int sy1 = std::min(h, std::max(sy0 + 1, int((y + 1) / scale)));
        for (int x = 0; x < int(w * scale + 0.5) && x < outW; x++) {
            const int sx0 = int(x / scale);
            const int sx1 = std::min(w, std::max(sx0 + 1, int((x + 1) / scale)));
            uint32_t sumL = 0, sumA = 0, n = 0;
            for (int sy = sy0; sy < sy1; sy++) {
                for (int sx = sx0; sx < sx1; sx++) {
                    const stbi_uc* px = data + (size_t(sy) * w + sx) * 4;
                    const uint32_t lum = (px[0] * 54 + px[1] * 183 + px[2] * 19) >> 8;
                    const uint32_t i = opaque ? 255u : lum;
                    const uint32_t a = opaque ? lum : px[3];
                    sumL += i * a / 255;
                    sumA += a;
                    n++;
                }
            }
            if (n > 0) {
                const size_t at = size_t(y) * outW + x;
                strip.intensity[at] = uint8_t(sumL / n);
                strip.alpha[at] = uint8_t(sumA / n);
            }
        }
    }

    stbi_image_free(data);
    if (opaque) {
        apply_shadow(strip);
    }
    return true;
}

uint32_t g_last_applied_seq = 0;
uint32_t g_last_applied_snd_seq = 0;
bool g_staged = false;
bool g_mailbox_seeded = false;

// ---------------------------------------------------------------------------
// The Option screen's dress for the pages over a course (graphics_menu_patch.c
// snap_course_options_page), texel for texel the screen's own sprites as
// they sit in the main menu's segment (src/main_menu/A0D4D0.c places them):
// the rule, 240x3 IA16, one opaque row of intensity 217 between two clear
// ones, drawn above and below the heading and as the help box's top and
// bottom; the box's side, 3x30, the middle column. Both are simple enough to
// write down; the header's legend is harvested whole instead
// (menu_harvest.cpp), being 32-bit with 8-bit alpha in its lettering.
// ---------------------------------------------------------------------------
// The stock sprites, texel for texel: the rule is 240x3 IA16, one opaque row
// of intensity 217 between two clear ones; the box's side 3x30, the middle
// column. They read on any screen because every page is drawn over the
// dim, which leaves at most 40 percent of what is behind: the line is
// always the brightest thing on its row.
Strip compose_rule() {
    Strip s;
    s.width = 256;
    s.height = 3;
    s.intensity.assign(size_t(s.width) * s.height, 217);
    s.alpha.assign(size_t(s.width) * s.height, 0);
    for (int x = 0; x < 240; x++) {
        s.alpha[size_t(1) * s.width + x] = 255;
    }
    return s;
}

Strip compose_box_side() {
    Strip s;
    s.width = 64;
    s.height = 30;
    s.intensity.assign(size_t(s.width) * s.height, 217);
    s.alpha.assign(size_t(s.width) * s.height, 0);
    for (int y = 0; y < s.height; y++) {
        s.alpha[size_t(y) * s.width + 1] = 255;
    }
    return s;
}

// ---------------------------------------------------------------------------
// The pause menu's fourth pill (pause_menu_patch.inc).
//
// The pause menu's three items are 89x19 RGBA16 sprites in the level code's
// data, each pill with its word baked in, a bright "selected" one and a dark
// plain one per item: blue, yellow, red. The port's item is a fourth pill in
// green -- the one colour of the set the menu does not use, and the B
// button's -- made from the yellow pair: the pill kept texel for texel, its
// word erased to the fill, every texel's hue turned by the one angle that
// puts the fill at green, and "Options" set in the pills' own letters at the
// pills' own centring (the left edge at (89 - core extent) / 2, which is
// where all three stock words stand). The O is the Q of "Quit Course"
// without its tail, its bottom the mirror of its top; the p is the n's stem
// and arch closed the way the o closes, with the one-row descender the y
// has; t, i, o, n and s are read off the words. Read from the ROM when the
// strings are staged: the level's segment is not in RDRAM at the main menu.
// ---------------------------------------------------------------------------
struct Rgba16Art {
    int w = 0;   // staged width: whole 64-texel blocks
    int h = 0;
    std::vector<uint16_t> texels;
};

namespace pill {

struct Rgba {
    uint8_t r = 0, g = 0, b = 0, a = 0;
};

// The level data segment's VRAM to ROM (asm/data/app_level/506FB0.data.s).
constexpr uint32_t kVramToRom = 0x8037D2A0u - 0x51D6B0u;
// The "Quit Course" pair's Sprite structs in the ROM: bright, then dark.
constexpr uint32_t kSelectedRom = 0x51D6B0u;
constexpr uint32_t kPlainRom = 0x51E4E8u;
constexpr int W = 89;
constexpr int H = 19;
constexpr int StagedW = 128;
// The word stands in rows 5..12 between the caps (columns 7..81), on a flat fill.
constexpr int WordTop = 5;
constexpr int WordBottom = 12;
constexpr int WordLeft = 7;
constexpr int WordRight = 81;
constexpr float TargetHue = 120.0f;   // green

// The letters as coverage in tenths, read off the three stock words: '9' is
// the word's colour outright, a digit d is (d + 0.5) / 10 of the way from
// the fill, '.' the fill. `top` is the glyph's first pill row, `core` its
// width without the antialiasing column most letters carry on the right.
struct Glyph {
    char ch;
    int top;
    int core;
    const char* rows[7];
};
constexpr Glyph kGlyphs[] = {
    { 'O', 5, 5, { "29990", "96279", "90.19", "9...9", "90.19", "96279", "29990" } },
    { 'p', 7, 3, { "996.", "9.91", "9092", "9.92", "9960", "9...", nullptr } },
    { 't', 5, 3, { ".2..", ".92.", "9992", ".9..", ".9..", ".9..", ".99." } },
    { 'i', 5, 1, { "9", "2", "9", "9", "9", "9", "9" } },
    { 'o', 7, 3, { "696.", "9190", "9092", "9193", "6960", nullptr, nullptr } },
    { 'n', 7, 3, { "996.", "9.91", "9092", "9.92", "9.92", nullptr, nullptr } },
    { 's', 7, 3, { "692", "922", "660", "292", "961", nullptr, nullptr } },
};
constexpr int LetterGap = 2;   // columns from one letter's core to the next's

const Glyph* glyph(char c) {
    for (const Glyph& g : kGlyphs) {
        if (g.ch == c) {
            return &g;
        }
    }
    return nullptr;
}

uint16_t rom_u16(std::span<const uint8_t> rom, uint32_t at) {
    return uint16_t((rom[at] << 8) | rom[at + 1]);
}

uint32_t rom_u32(std::span<const uint8_t> rom, uint32_t at) {
    return (uint32_t(rom[at]) << 24) | (uint32_t(rom[at + 1]) << 16) | (uint32_t(rom[at + 2]) << 8) | rom[at + 3];
}

// One stock pill out of the ROM: RGBA16, three bitmaps side by side,
// pre-shuffled for TMEM (the odd rows' words swapped, undone here as
// menu_harvest.cpp decode_sprite undoes it).
bool decode(std::span<const uint8_t> rom, uint32_t spriteRom, std::vector<Rgba>& img) {
    if (rom.size() < 0x530000u) {
        return false;
    }
    const int width = int16_t(rom_u16(rom, spriteRom + 0x04));
    const int height = int16_t(rom_u16(rom, spriteRom + 0x06));
    const uint16_t attr = rom_u16(rom, spriteRom + 0x14);
    const int nbitmaps = int16_t(rom_u16(rom, spriteRom + 0x28));
    const int bmheight = int16_t(rom_u16(rom, spriteRom + 0x2C));
    const uint16_t fmtsiz = rom_u16(rom, spriteRom + 0x30);
    const uint32_t bitmapVram = rom_u32(rom, spriteRom + 0x34);
    if ((width != W) || (height != H) || (nbitmaps <= 0) || (nbitmaps > 8) || (bmheight <= 0) ||
        (fmtsiz != 0x0002) || (bitmapVram < kVramToRom)) {
        return false;
    }
    const uint32_t bitmaps = bitmapVram - kVramToRom;
    if (bitmaps + uint32_t(nbitmaps) * 0x10u > rom.size()) {
        return false;
    }
    const bool shuffled = (attr & 0x200u) != 0;
    img.assign(size_t(W) * H, Rgba{});
    int x0 = 0;
    int y0 = 0;
    for (int bi = 0; bi < nbitmaps; bi++) {
        const uint32_t b = bitmaps + uint32_t(bi) * 0x10u;
        const int bw = int16_t(rom_u16(rom, b + 0x0));
        const int bwImg = int16_t(rom_u16(rom, b + 0x2));
        const uint32_t bufVram = rom_u32(rom, b + 0x8);
        int rows = int16_t(rom_u16(rom, b + 0xC));
        if (rows == 0) {
            rows = bmheight;
        }
        if ((bw <= 0) || (bwImg < bw) || (rows <= 0) || (bufVram < kVramToRom)) {
            return false;
        }
        const uint32_t buf = bufVram - kVramToRom;
        const int rowBytes = bwImg * 2;
        if (buf + uint32_t(rowBytes) * uint32_t(rows) > rom.size()) {
            return false;
        }
        if (x0 >= W) {
            x0 = 0;
            y0 += bmheight;
        }
        for (int ry = 0; (ry < rows) && (y0 + ry < H); ry++) {
            for (int p = 0; (p < bw) && (x0 + p < W); p++) {
                int o = p * 2;
                if (shuffled && (ry & 1) && (o < (rowBytes & ~7))) {
                    o ^= 4;
                }
                const uint16_t v = rom_u16(rom, buf + uint32_t(ry * rowBytes + o));
                Rgba& px = img[size_t(y0 + ry) * W + size_t(x0 + p)];
                const int r = (v >> 11) & 31;
                const int g = (v >> 6) & 31;
                const int bl = (v >> 1) & 31;
                px.r = uint8_t((r << 3) | (r >> 2));
                px.g = uint8_t((g << 3) | (g >> 2));
                px.b = uint8_t((bl << 3) | (bl >> 2));
                px.a = (v & 1) ? 255 : 0;
            }
        }
        x0 += bw;
    }
    return true;
}

void rgb_to_hsv(const Rgba& p, float& h, float& s, float& v) {
    const float r = p.r / 255.0f;
    const float g = p.g / 255.0f;
    const float b = p.b / 255.0f;
    const float mx = std::max(r, std::max(g, b));
    const float mn = std::min(r, std::min(g, b));
    const float d = mx - mn;
    v = mx;
    s = (mx > 0.0f) ? (d / mx) : 0.0f;
    if (d <= 0.0f) {
        h = 0.0f;
    }
    else if (mx == r) {
        h = 60.0f * std::fmod((g - b) / d, 6.0f);
    }
    else if (mx == g) {
        h = 60.0f * ((b - r) / d + 2.0f);
    }
    else {
        h = 60.0f * ((r - g) / d + 4.0f);
    }
    if (h < 0.0f) {
        h += 360.0f;
    }
}

Rgba hsv_to_rgb8(float h, float s, float v, uint8_t a) {
    h = std::fmod(h, 360.0f);
    if (h < 0.0f) {
        h += 360.0f;
    }
    const float c = v * s;
    const float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    const float m = v - c;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    if (h < 60.0f) { r = c; g = x; }
    else if (h < 120.0f) { r = x; g = c; }
    else if (h < 180.0f) { g = c; b = x; }
    else if (h < 240.0f) { g = x; b = c; }
    else if (h < 300.0f) { r = x; b = c; }
    else { r = c; b = x; }
    Rgba out;
    out.r = uint8_t(std::lround((r + m) * 255.0f));
    out.g = uint8_t(std::lround((g + m) * 255.0f));
    out.b = uint8_t(std::lround((b + m) * 255.0f));
    out.a = a;
    return out;
}

Rgba turned(const Rgba& p, float delta) {
    float h, s, v;
    rgb_to_hsv(p, h, s, v);
    return hsv_to_rgb8(h + delta, s, v, p.a);
}

} // namespace pill

// One state of the pill: the stock "Quit Course" pill of that state, its
// word erased, turned to green, "Options" set on it, staged as RGBA16 in
// two 64-texel blocks (the pill's 89 columns, then clear). False when the
// ROM is not there to read or its pill is not the sprite expected; the
// pause menu then keeps the cartridge's three items.
bool compose_pause_pill(bool selected, Rgba16Art& out) {
    using namespace pill;
    const std::span<const uint8_t> rom = recomp::get_rom();
    std::vector<Rgba> img;
    if (!decode(rom, selected ? kSelectedRom : kPlainRom, img)) {
        return false;
    }
    auto at = [&](int x, int y) -> Rgba& { return img[size_t(y) * W + size_t(x)]; };

    // The fill, and the word's own colour: white on the bright pill, a
    // neutral grey on the dark one.
    const Rgba fill = at(12, 9);
    Rgba word = fill;
    int brightest = -1;
    for (int y = WordTop; y <= WordBottom; y++) {
        for (int x = WordLeft; x <= WordRight; x++) {
            const Rgba p = at(x, y);
            const int sum = p.r + p.g + p.b;
            if ((p.a != 0) && (sum > brightest)) {
                brightest = sum;
                word = p;
            }
        }
    }
    for (int y = WordTop; y <= WordBottom; y++) {
        for (int x = WordLeft; x <= WordRight; x++) {
            at(x, y) = fill;
        }
    }

    // Yellow to green: the fill's hue to 120 degrees, every texel by the
    // same turn, so the caps' shading, the rim and the outline follow.
    float fh, fs, fv;
    rgb_to_hsv(fill, fh, fs, fv);
    const float delta = TargetHue - fh;
    for (Rgba& p : img) {
        if (p.a != 0) {
            p = turned(p, delta);
        }
    }
    const int spread = int(std::max(word.r, std::max(word.g, word.b))) - int(std::min(word.r, std::min(word.g, word.b)));
    const Rgba ink = (spread < 24) ? word : turned(word, delta);

    // "Options", centred as the stock words are.
    const char* text = "Options";
    int extent = -LetterGap;
    for (const char* c = text; *c != 0; c++) {
        const Glyph* g = glyph(*c);
        if (g == nullptr) {
            return false;
        }
        extent += g->core + LetterGap;
    }
    int x = (W - extent) / 2;
    for (const char* c = text; *c != 0; c++) {
        const Glyph* g = glyph(*c);
        for (int dy = 0; (dy < 7) && (g->rows[dy] != nullptr); dy++) {
            const char* row = g->rows[dy];
            for (int dx = 0; row[dx] != 0; dx++) {
                if (row[dx] == '.') {
                    continue;
                }
                const int d = row[dx] - '0';
                const float cov = (d >= 9) ? 1.0f : (float(d) + 0.5f) / 10.0f;
                Rgba& p = at(x + dx, g->top + dy);
                p.r = uint8_t(std::lround(p.r + cov * (int(ink.r) - int(p.r))));
                p.g = uint8_t(std::lround(p.g + cov * (int(ink.g) - int(p.g))));
                p.b = uint8_t(std::lround(p.b + cov * (int(ink.b) - int(p.b))));
                p.a = 255;
            }
        }
        x += g->core + LetterGap;
    }

    out.w = StagedW;
    out.h = H;
    out.texels.assign(size_t(StagedW) * H, 0);
    for (int y = 0; y < H; y++) {
        for (int xx = 0; xx < W; xx++) {
            const Rgba& p = at(xx, y);
            if (p.a != 0) {
                out.texels[size_t(y) * StagedW + xx] =
                    uint16_t(((p.r >> 3) << 11) | ((p.g >> 3) << 6) | ((p.b >> 3) << 1) | 1);
            }
        }
    }
    return true;
}

// The BUTTON SETUP page's bank of the mailbox, at +0xA0 (the byte map is in the
// patch, SNAP_GFX_MAILBOX): the page writes a request word and the device
// it shows; the host answers with an ack word, keeps the row values
// composed for that device, and says whether a pad is attached.
constexpr uint32_t BindReqAddr    = MailboxAddr + 0xA0;
constexpr uint32_t BindAckAddr    = MailboxAddr + 0xA4;
constexpr uint32_t BindGenAddr    = MailboxAddr + 0xA8;
constexpr uint32_t BindDeviceAddr = MailboxAddr + 0xAC;
constexpr uint32_t BindOpenAddr   = MailboxAddr + 0xAD;
constexpr uint32_t BindPadAddr    = MailboxAddr + 0xAE;
// The Mods page's bank (graphics_menu_patch.c, the MODS_ defines).
constexpr uint32_t ModsReqAddr    = MailboxAddr + 0xB0;
constexpr uint32_t ModsAckAddr    = MailboxAddr + 0xB4;
constexpr uint32_t ModsGenAddr    = MailboxAddr + 0xB8;
constexpr uint32_t ModsCountAddr  = MailboxAddr + 0xBC;
constexpr uint32_t ModsOpenAddr   = MailboxAddr + 0xBE;
constexpr uint32_t ModsTopAddr    = MailboxAddr + 0xBF;
constexpr uint32_t ModsStateAddr  = MailboxAddr + 0xC0;
constexpr uint32_t ModsHasOptAddr = MailboxAddr + 0xC2;   // + bank: the window's rows with options
constexpr uint32_t ModsModCountAddr = MailboxAddr + 0xC4; // u16: the mods; the rows after are the actions
constexpr int ModsActions = 2;                            // Open the mods folder, Restart the game

// A mod's options page (graphics_menu_patch.c snap_mod_options_page): its
// own bank of the mailbox and its own dynamic slots -- two banks of six
// names, six values and six help lines for the window shown.
constexpr uint32_t OptReqAddr   = MailboxAddr + 0xC8;
constexpr uint32_t OptAckAddr   = MailboxAddr + 0xCC;
constexpr uint32_t OptGenAddr   = MailboxAddr + 0xD0;
constexpr uint32_t OptCountAddr = MailboxAddr + 0xD4;
constexpr uint32_t OptOpenAddr  = MailboxAddr + 0xD6;
constexpr uint32_t OptTopAddr   = MailboxAddr + 0xD7;
constexpr uint32_t OptRowAddr   = MailboxAddr + 0xD8;
constexpr uint32_t kOptDynBase = kStringBaseCount + 238;   // graphics_menu_patch.c STR_OPT_DYN
constexpr int OptVisible = 6;
constexpr int OptBank = 3 * OptVisible;
constexpr uint32_t OptNamesAddr = ModsHelpsAddr + uint32_t(2 * ModsVisible) * ModsHelpBytes;
constexpr uint32_t OptValuesAddr = OptNamesAddr + uint32_t(2 * OptVisible) * DynStripBytes;
constexpr uint32_t OptHelpsAddr = OptValuesAddr + uint32_t(2 * OptVisible) * DynStripBytes;
constexpr int OptNameInkWidth = 105;    // the label column, before the values at x=163
constexpr int OptValueInkWidth = 110;   // the value column, to the right rail
static uint32_t opt_dyn_addr(uint32_t id) {
    const uint32_t k = id - kOptDynBase;
    const uint32_t bank = k / uint32_t(OptBank);
    const uint32_t slot = k % uint32_t(OptBank);
    if (slot < uint32_t(OptVisible)) {
        return OptNamesAddr + (bank * uint32_t(OptVisible) + slot) * DynStripBytes;
    }
    if (slot < uint32_t(2 * OptVisible)) {
        return OptValuesAddr + (bank * uint32_t(OptVisible) + (slot - uint32_t(OptVisible))) * DynStripBytes;
    }
    return OptHelpsAddr + (bank * uint32_t(OptVisible) + (slot - uint32_t(2 * OptVisible))) * ModsHelpBytes;
}
// The pages from anywhere: the host's word that the menu key was pressed in
// a course, for the pause code, and the patch's word that a page is up.
constexpr uint32_t MenuReqAddr   = MailboxAddr + 0x39;
constexpr uint32_t PagesOpenAddr = MailboxAddr + 0x3D;
constexpr uint32_t PauseAliveAddr = MailboxAddr + 0x3E;   // the pause handler's heartbeat, counted down here
constexpr uint32_t RunnerNoteAddr = MailboxAddr + 0x3F;   // the runner's word on a press, for the log

// The pages' own memory (anywhere_patch.inc carries the same numbers). The
// scene's general heap (sys/gtl.c) is a bump allocator the object manager
// grows its pools from when a free list runs dry, and its overflow is the
// game's panic, an endless loop the port parks with no message. A screen
// that has spent its heap -- the lab's first frames, the photo screens --
// cannot give the pages one object, so the heap is pointed at this arena,
// in RDRAM beyond anything the cartridge addresses, while the runner is
// made here and while its pages are up. The objects grown there stay in
// the scene's free lists until the scene goes, when the cursor moves back.
constexpr uint32_t ArenaStart   = 0x80E00000u;
constexpr uint32_t ArenaEnd     = 0x80E40000u;
constexpr uint32_t ArenaPtrAddr = MailboxAddr + 0xF0;   // u32: the arena's cursor
constexpr uint32_t HeapFreeAddr = MailboxAddr + 0xF4;   // u32: the scene heap's free bytes at the press
constexpr uint32_t ObjStatAddr  = MailboxAddr + 0xF8;   // u32: active objects << 16 | the limit
constexpr uint32_t SceneAgeAddr = MailboxAddr + 0xFC;   // u32: the ticks this scene has run, for the runner
// The room the screen's main display list buffer had left last frame
// (src/dl_budget.cpp measures it), and what the pages may draw at most: the
// same number as SNAP_PAGES_DL_NEED in graphics_menu_patch.c.
constexpr uint32_t DlSpareAddr = MailboxAddr + 0xE8;
constexpr uint32_t PagesDlNeed = 8192;
constexpr uint32_t kGeneralHeap  = 0x8004A8C8u;   // sys/gtl.c sGeneralHeap: id, start, end, ptr
constexpr uint32_t kOmMaxObjects = 0x8004AC02u;   // sys/om.c omMaxObjects, s16
// The page's eighteen input rows, in its order (graphics_menu_patch.c
// STR_BIND_INPUT): the names input.h gives them.
const char* const kBindInputs[BindInputCount] = {
    "a", "b", "z", "start", "l", "r", "c_up", "c_down", "c_left", "c_right",
    "d_up", "d_down", "d_left", "d_right", "stick_up", "stick_down", "stick_left", "stick_right",
};
uint32_t g_bind_handled = 0;      // the request last answered; 0 while none is pending
bool g_bind_capturing = false;
int g_bind_shown_device = -1;     // the device the composed bank is for
uint32_t g_bind_shown_gen = 0;    // the binding table's generation it was composed from

// The rainbow strips keep their alpha masks host-side so their staged
// RGBA16 texels can be recoloured live -- exactly the kind of
// colour-cycled flourish the era loved. The sprite reads RDRAM every
// frame, so rewriting the texels is the whole animation. The credits
// line wears it as the port's signature, and the Graphics page's scroll
// arrows wear the same one: the port's marks speak one language.
struct RainbowStrip {
    uint32_t addr = 0;
    int w = 0, h = 0;
    std::vector<uint8_t> mask;
};
RainbowStrip g_credits;
RainbowStrip g_arrows[2];

void hsv_to_rgb(int hue, uint8_t value, uint8_t &r, uint8_t &g, uint8_t &b) {
    // Saturation fixed at ~0.72 so every hue stays luminous on screen.
    const int sector = (hue / 60) % 6;
    const int f = hue % 60;
    const uint8_t lo = uint8_t(value * 28 / 100);
    const uint8_t up = uint8_t(lo + (value - lo) * f / 60);
    const uint8_t dn = uint8_t(value - (value - lo) * f / 60);
    switch (sector) {
        case 0:  r = value; g = up;    b = lo;    break;
        case 1:  r = dn;    g = value; b = lo;    break;
        case 2:  r = lo;    g = value; b = up;    break;
        case 3:  r = lo;    g = dn;    b = value; break;
        case 4:  r = up;    g = lo;    b = value; break;
        default: r = value; g = lo;    b = dn;    break;
    }
}

// A calm scrolling rainbow at constant full brightness -- the classic
// era treatment -- drifting one hue degree a tick, roughly a six second
// lap. No pulsing: the black border carries the legibility and the
// colour quietly moves.
void animate_rainbow(const RainbowStrip& s, uint32_t tick) {
    if (s.addr == 0) {
        return;
    }
    const int w = s.w;
    const int h = s.h;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (s.mask[size_t(y) * w + x] < 128) {
                continue;
            }
            uint8_t r, g, b;
            hsv_to_rgb(int((tick + x * 3) % 360), 0xFF, r, g, b);
            const uint16_t texel = uint16_t(((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | 1);
            const int chunk = x / 64;
            const uint32_t off = uint32_t((chunk * 64 * h + y * 64 + (x % 64)) * 2);
            write_u16(s.addr + off, texel);
        }
    }
}

void animate_credits() {
    static uint32_t tick = 0;
    tick++;
    animate_rainbow(g_credits, tick);
    // The scroll arrows sweep on the same clock; their few rows cost
    // nothing when the Graphics page is closed and their sprite hidden.
    animate_rainbow(g_arrows[0], tick);
    animate_rainbow(g_arrows[1], tick);
}

// The CONTROLS page's speed steps, as percentages of the shipped speed, and
// the zoom share's four; the page shows these numbers and stores an index.
// The speeds are settings.h's kMouseSpeedSteps, shared with the hotkeys.
static const int* const kCtlSpeeds = kMouseSpeedSteps;
static const int kCtlZoomShares[4] = { 25, 50, 75, 100 };
static uint32_t g_last_applied_ctl_seq = 0;

// The Frame Rate row's rates: its values past Original and Display are
// these, the Manual mode held at that rate by interpolation. A file may
// carry any rate; the row shows the nearest of these, and that becomes
// the rate once the page is edited.
static const int kFpsTargets[6] = { 60, 90, 120, 144, 165, 240 };

static int fps_target_index(int hz) {
    int best = 2;
    for (int i = 0; i < 6; i++) {
        if (std::abs(kFpsTargets[i] - hz) < std::abs(kFpsTargets[best] - hz)) best = i;
    }
    return best;
}

static int ctl_speed_index(float sensitivity) {
    const int pct = int(std::lround(sensitivity * 100.0f));
    int best = 3;
    for (int i = 0; i < 11; i++) {
        if (std::abs(kCtlSpeeds[i] - pct) < std::abs(kCtlSpeeds[best] - pct)) best = i;
    }
    return best;
}

static int ctl_zoom_index(float share) {
    const int pct = int(std::lround(share * 100.0f));
    int best = 1;
    for (int i = 0; i < 4; i++) {
        if (std::abs(kCtlZoomShares[i] - pct) < std::abs(kCtlZoomShares[best] - pct)) best = i;
    }
    return best;
}

// The setting bytes of the three banks, from the settings as they are now.
// Seeded at every overlay load, and written again whenever a setting
// changes outside the pages (menu_mailbox_sync): the pages read these
// bytes as the truth and write every one of them back on any edit, so a
// byte that lagged the setting undid it -- fullscreen entered from the
// maximize button or F11 left byte 0xF at zero, and the next edit of any
// row on the Graphics page dropped the window (issue #13, flamespeedy on
// Windows and dCo3lh0 on Linux, 1.0.4).
static void write_setting_bytes(const Settings &s) {
    write_u8(MailboxAddr + 0x8, uint8_t(s.resolution_scale));
    const uint8_t msaaIndex = (s.msaa >= 8) ? 3 : (s.msaa >= 4) ? 2 : (s.msaa >= 2) ? 1 : 0;
    write_u8(MailboxAddr + 0x9, msaaIndex);
    write_u8(MailboxAddr + 0xA, s.widescreen ? 1 : 0);
    // Frame Rate: 0 Original, 1 Display, 2 on a held rate (kFpsTargets).
    {
        uint8_t fps = 0;
        if (s.fps_mode == 1) {
            fps = 1;
        } else if (s.fps_mode == 2) {
            fps = uint8_t(2 + fps_target_index(s.fps_manual_target));
        }
        write_u8(MailboxAddr + 0xB, fps);
    }
    write_u8(MailboxAddr + 0xC, uint8_t(s.upscale_2d));
    write_u8(MailboxAddr + 0xD, uint8_t(s.present_filter));
    write_u8(MailboxAddr + 0xE, s.dither_noise ? 1 : 0);
    write_u8(MailboxAddr + 0xF, s.fullscreen ? 1 : 0);
    write_u8(MailboxAddr + 0x10, uint8_t(std::clamp(s.downsample, 1, 8) - 1));
    write_u8(MailboxAddr + 0x11, s.three_point_filtering ? 0 : 1);
    write_u8(MailboxAddr + 0x12, uint8_t(std::clamp(s.color_depth, 0, 2)));
    write_u8(MailboxAddr + 0x13, s.triple_buffering ? 1 : 0);
    write_u8(MailboxAddr + 0x14, s.crop_enabled ? 1 : 0);
    // Read by the intro patches themselves (patches/src/beach_intro_patch.c,
    // river_intro_patch.c) once, as a course intro starts -- this byte
    // reaches the game whether or not the page is ever opened, on every
    // overlay load's re-seed.
    write_u8(MailboxAddr + 0x15, s.intro_fix ? 1 : 0);
    // Fields 14 and 15 fill the bank: +0x18 is the patch's own pointer
    // word (the byte map at SNAP_GFX_MAILBOX in the patch).
    write_u8(MailboxAddr + 0x16, s.photo_detail ? 1 : 0);
    write_u8(MailboxAddr + 0x17, s.jynx_vc ? 1 : 0);
    // The SOUND bank's six value bytes, read live by the patched audio
    // functions (volumes as straight percentages) and edited by the SOUND
    // page.
    write_u8(MailboxAddr + 0x28, uint8_t(std::clamp(s.master_volume, 0, 100)));
    write_u8(MailboxAddr + 0x29, uint8_t(std::clamp(s.music_volume, 0, 100)));
    write_u8(MailboxAddr + 0x2A, uint8_t(std::clamp(s.sfx_volume, 0, 100)));
    write_u8(MailboxAddr + 0x2B, uint8_t(std::clamp(s.shutter_volume, 0, 100)));
    write_u8(MailboxAddr + 0x2C, s.stereo ? 1 : 0);
    write_u8(MailboxAddr + 0x2D, s.mute_unfocused ? 1 : 0);
    // The CONTROLS bank's value bytes from +0x64: mouse aim, the mouse
    // speed's index into the page's eleven steps, the zoom speed's index
    // into its four, the tilt, gyro aim, the gyro speed's index.
    write_u8(MailboxAddr + 0x64, s.mouse_aim ? 1 : 0);
    write_u8(MailboxAddr + 0x65, uint8_t(ctl_speed_index(s.mouse_sensitivity)));
    write_u8(MailboxAddr + 0x66, uint8_t(ctl_zoom_index(s.mouse_zoom_speed)));
    write_u8(MailboxAddr + 0x67, s.mouse_invert_y ? 1 : 0);
    write_u8(MailboxAddr + 0x68, uint8_t(std::clamp(s.gyro_aim, 0, 2)));
    write_u8(MailboxAddr + 0x69, uint8_t(ctl_speed_index(s.gyro_sensitivity)));
    write_u8(MailboxAddr + 0x6A, s.pad_sticks_swapped ? 1 : 0);
    // The dead zone in steps of five: the page shows 0, 5, 10 .. 40.
    write_u8(MailboxAddr + 0x6B, uint8_t(std::clamp((s.pad_deadzone + 2) / 5, 0, 8)));
    // Fast Forward: 0 Off, then 2x, 3x, 4x as 1..3.
    write_u8(MailboxAddr + 0x6C, uint8_t(std::clamp(s.fast_forward_speed, 1, 4) - 1));
    // Slow Motion: 0 Off, 1 half speed, 2 a quarter.
    write_u8(MailboxAddr + 0x6D, uint8_t((s.slow_motion_speed >= 4) ? 2 : ((s.slow_motion_speed >= 2) ? 1 : 0)));
}

void seed_mailbox() {
    write_setting_bytes(settings());
    write_u8(MailboxAddr + 0x38, 0);   // no title request pending
    write_u8(MailboxAddr + 0x3A, 0);   // no Exit Game request pending
    write_u8(MailboxAddr + 0x3C, 0);   // the title menu has not been built yet
    // Host-owned: the renderer's horizontal widening for the culling patch
    // (settings.h, set_view_wide_q8); refreshed every tick below.
    write_u32(MailboxAddr + 0x44, view_wide_q8());
    write_u32(MailboxAddr + 0x48, 0);
    write_u32(MailboxAddr + 0x4, 0);
    // The SOUND bank's sequence word (its value bytes are above), and the
    // CONTROLS bank's at +0x60. The pages bump them on an edit; the host
    // applies on each bump (poll_menu_mailbox).
    write_u32(MailboxAddr + 0x20, 0);
    write_u32(MailboxAddr + 0x60, 0);
    // The BUTTON SETUP page's bank: no request, no answer, bank zero, closed.
    write_u32(BindReqAddr, 0);
    write_u32(BindAckAddr, 0);
    write_u32(BindGenAddr, 0);
    write_u32(MailboxAddr + 0xAC, 0);
    g_bind_handled = 0;
    g_bind_capturing = false;
    g_bind_shown_device = -1;
    write_u32(MailboxAddr + 0x0, MailboxMagic);
    g_last_applied_seq = 0;
    g_last_applied_snd_seq = 0;
    g_last_applied_ctl_seq = 0;
}

} // namespace

// A setting changed outside the pages (a hotkey, the maximize button, the
// fullscreen restored after the window opened): the pages' bytes are made
// to say so, or their next edit would write the old value back.
void menu_mailbox_sync() {
    if ((g_menu_rdram == nullptr) || !g_mailbox_seeded) {
        return;
    }
    if (read_u32_mail(MailboxAddr) != MailboxMagic) {
        return;
    }
    write_setting_bytes(settings());
}

// Seeds the settings mailbox. Called on every overlay load
// (src/overlay_hook.cpp): the menu cannot be open while code is being
// swapped, so this is a safe moment to refresh the bytes with whatever the
// hotkeys changed, and the first call at boot is what the intro and
// SFX-volume patches read from.
void stage_menu_assets(uint8_t* rdram) {
    if (rdram == nullptr) {
        return;
    }
    g_menu_rdram = rdram;
    seed_mailbox();
    g_mailbox_seeded = true;
}

// Harvests the menu font out of the main menu's freshly decompressed VPK0
// segment, then composites and stages every string the GRAPHICS and SOUND
// pages and the title screen draw. Called from the dmaReadVPK0 wrapper the
// moment that segment lands (src/overlay_hook.cpp). Idempotent once it has
// succeeded; a failed harvest stages nothing, leaves the directory magic
// unwritten so the game's own screens run as shipped, and is retried on the
// next main-menu load.
void stage_menu_strings(uint8_t* rdram) {
    if (rdram == nullptr) {
        return;
    }
    g_menu_rdram = rdram;
    if (g_staged) {
        return;
    }
    if (!harvest_menu_font(rdram, g_font)) {
        printf("[SNAP-MENU] the Options screen stays as shipped until the next main menu load\n");
        return;
    }
    g_missing_glyphs = 0;

    const char* strings[] = {
        "",                                    // 0: 16x16 black tile (unused by the page now)
        "Graphics",                            // 1: the new Option item's label
        "Render Scale",                        // 2
        "Anti-Aliasing",                       // 3
        "Widescreen",                          // 4
        "Frame Rate",                          // 5
        "2D Detail",                           // 6
        "Filter",                              // 7
        "Dither",                              // 8
        "Fullscreen",                          // 9
        "Display and renderer settings.",      // 10: help line for the item
        "< Auto >",                            // 11
        "< Off >",                             // 12
        "< On >",                              // 13
        "< 1x >", "< 2x >", "< 3x >", "< 4x >",    // 14..17
        "< 5x >", "< 6x >", "< 7x >", "< 8x >",    // 18..21
        "< Original >",                        // 22
        "< Display >",                         // 23
        "< Classic >",                         // 24
        "< Sharp >",                           // 25
        "< Point >",                           // 26
        "< Smooth >",                          // 27
        "< Crisp >",                           // 28
        "Left and Right change the setting.",  // 29: kept for PNG-override compatibility
    };
    constexpr uint32_t BaseCount = uint32_t(sizeof(strings) / sizeof(strings[0]));

    // What each page row does, two lines apiece -- swapped into the help box
    // as the selection moves, the way the stock screen's helps follow it.
    // Ids BaseCount..BaseCount+7.
    static const char* const descs[8][2] = {
        { "Sets the 3D rendering resolution.",         "Auto follows the window size." },
        { "Smooths jagged edges on 3D models.",        "Higher levels cost more performance." },
        { "Widens the view for widescreen displays.",  "The picture stays undistorted." },
        { "Original is the console pace. Display",        "or a number smooths to that rate." },
        { "Classic keeps 2D art at its original size.","Sharp redraws all, and may fringe logos." },
        { "How the picture is scaled to the window.",  "Crisp keeps pixels sharp and smooth." },
        { "The original console dither pattern.",      "Adds fine noise to smooth gradients." },
        { "Switches between fullscreen and window.",   "" },
    };
    // Id BaseCount+22: the "Recomp" wordmark drawn under the Snap logo on
    // the title screen -- the port's one badge. Full colour, RGBA16, loaded
    // from menu_text/recomp_logo.png; absent file, absent badge.
    // Id BaseCount+8: the page's heading, in the Options title's own face.
    // Ids BaseCount+9..BaseCount+17: the second wave of settings -- labels
    // and values for the rows added when the page grew past the original
    // eight. Ids BaseCount+18..BaseCount+21: their descriptions.
    static const char* const extraStrings[9] = {
        "Super Sampling",                      // +9
        "Texture Filter",                      // +10
        "Color Depth",                         // +11
        "Buffering",                           // +12
        "< Authentic >",                       // +13
        "< Standard >",                        // +14
        "< High >",                            // +15
        "< Double >",                          // +16
        "< Triple >",                          // +17
    };
    static const char* const extraDescs[4][2] = {
        { "Renders above native and downsamples.",   "The cleanest image, at a heavy cost." },
        { "Authentic keeps the N64 three point look.","Smooth uses standard bilinear filtering." },
        { "High reduces banding in gradients.",      "Takes effect after restarting the game." },
        { "Triple buffering smooths frame delivery.","Takes effect after restarting the game." },
    };
    // Id BaseCount+23: the title's third credits line, in the copyright
    // block's own condensed face, recoloured live by animate_credits().
    // Ids BaseCount+24/+25: the Graphics page's scroll arrows.
    // Id BaseCount+26: the SOUND page's heading. Ids +27..+45: its labels
    // and values (six labels, the eleven shared volume steps, Stereo and
    // Mono). Ids +46..+51: its six descriptions.
    static const char* const sndStrings[19] = {
        "Master Volume",                       // +27
        "Music Volume",                        // +28
        "Sound Effects",                       // +29
        "Shutter Volume",                      // +30
        "Speaker Output",                      // +31
        "Background Mute",                     // +32
        "< 0 >", "< 10 >", "< 20 >", "< 30 >", "< 40 >",   // +33..+37
        "< 50 >", "< 60 >", "< 70 >", "< 80 >", "< 90 >",  // +38..+42
        "< 100 >",                             // +43
        "< Stereo >",                          // +44
        "< Mono >",                            // +45
    };
    static const char* const sndDescs[6][2] = {
        { "Scales all sound the game makes.",         "The other sliders sit under it." },
        { "Sets the background music level.",         "Changes apply right away." },
        { "Sets the sound effects level.",            "Changes apply right away." },
        { "Sets the camera shutter volume.",          "The photo still scores the same." },
        { "Stereo suits speakers and headphones.",    "Mono mixes both sides together." },
        { "Silences the game while another",          "window is in front." },
    };
    // Ids BaseCount+52..+59: the Graphics page's last four rows -- Overscan
    // Crop, Cutscene Fix, Photo Detail, Jynx Recolor -- a label and a
    // description apiece. Id BaseCount+60: the title screen's "Snap Station"
    // item, in the title's own face (zero width when that face is missing).
    // Ids BaseCount+61..+90: the CONTROLS page. +61 its heading (header
    // face), +62 the Option item's label (with the dot), +63 the item's help
    // line (help face), +64..+69 its six row labels, +70..+73 the values
    // Hold, Switch, Normal, Reverse, +74..+84 the eleven mouse speeds,
    // +85..+90 its six descriptions.
    static const char* const ctlStrings[21] = {
        "Z Button",                            // +64
        "Control Stick",                       // +65
        "Mouse Aim",                           // +66
        "Mouse Speed",                         // +67
        "Zoom Speed",                          // +68
        "Camera Tilt",                         // +69
        "< Hold >",                            // +70
        "< Switch >",                          // +71
        "< Normal >",                          // +72
        "< Reverse >",                         // +73
        "< 25 >", "< 50 >", "< 75 >", "< 100 >", "< 125 >", "< 150 >",   // +74..+79
        "< 175 >", "< 200 >", "< 250 >", "< 300 >", "< 400 >",           // +80..+84
    };
    // The help face has no E, F, G, I, K, Q, U, V, X or Y, no digits but
    // 2, 3, 4 and 6, and no hyphen or apostrophe; every line below is set
    // within that.
    // Each line at most 41 characters, the widest the stock help box
    // shows without crowding its frame.
    static const char* const ctlDescs[6][2] = {
        { "Hold zooms while Z is held down.",           "Switch zooms on a press, off on the next." },
        { "Normal tilts the camera up with the stick",  "pushed up. Reverse tilts it down instead." },
        { "Move the mouse to look around a course.",    "Off leaves the camera to the stick." },
        { "How far the mouse turns the camera.",        "Lower is slower, higher is faster." },
        { "Mouse speed while zoomed in, as a share",    "of the normal speed. Lower is steadier." },
        { "Normal tilts up as the mouse or pad goes",  "forward. Reverse tilts down instead." },
    };
    // Ids BaseCount+91..+95: the CONTROLS page's two gyro rows, added
    // after the six above (the page scrolls for them): +91..+92 the labels,
    // +93 the value Zoomed, +94..+95 the descriptions.
    static const char* const gyroStrings[3] = {
        "Gyro Aim",                            // +91
        "Gyro Speed",                          // +92
        "< Zoomed >",                          // +93
    };
    // The CONTROLS page's Dead Zone row (id BaseCount+154), then Pad
    // Sticks (BaseCount+152).
    static const char* const deadzoneDesc[2] = { "How far the stick moves before the game", "sees it. Raise it if the camera drifts." };
    // The CONTROLS page's Fast Forward row (ids BaseCount+198 and +199): its
    // values are Off and the Graphics page's 2x, 3x and 4x.
    static const char* const fastDesc[2] = { "Hold Tab or the right shoulder button to", "run the game this many times as fast." };
    // The CONTROLS page's Slow Motion row (ids BaseCount+200 and +201): its
    // values are Off and the Graphics page's 2x and 4x.
    static const char* const slowDesc[2] = { "Hold Space or press the left stick in to", "run the game this many times slower." };
    static const char* const sticksDesc[2] = { "Normal aims with the left stick, the", "right does the C buttons. Swapped flips." };
    static const char* const gyroDescs[2][2] = {
        { "Turn the pad to look around a course, on",  "pads with a gyro. Zoomed aims zoomed in." },
        { "How far turning the pad turns the camera.", "Lower is slower, higher is faster." },
    };
    // Ids BaseCount+96..+98: the Option list's Exit Game item -- its label
    // with the dot, its help line, and the question the help line becomes
    // once it is chosen (graphics_menu_patch.c STR_EXIT_*).
    // Ids BaseCount+99..+152: the BUTTON SETUP page (graphics_menu_patch.c
    // STR_BTN_* and STR_BIND_*). +99 the CONTROLS page's Button Setup row
    // label, +100 that row's description; +101 the page's heading (header
    // face, its B, M and g the port's), +102 the Device row's label,
    // +103..+120 the eighteen input rows' labels, +121 Restore Defaults,
    // +122..+124 the Device row's values, +125..+131 the page's seven help
    // lines (bindDescs), +132..+149 the input rows' own (bindInputDescs);
    // +150 the CONTROLS page's Pad Sticks label, +151 its Swapped value,
    // +152 its description; +153 its Dead Zone label, +154 its
    // description, +155..+157 the values 5, 15 and 35 the volume and
    // speed steps do not already carry; +158..+161 the Frame Rate row's
    // 120, 144, 165 and 240 (its 60 and 90 are volume steps).
    // Ids BaseCount+162..+197: the input rows' values, composed live for
    // the device shown (bind_compose below), two banks of eighteen so a
    // bank is never rewritten while the page draws it; their pixels sit
    // apart from the staged strings, at DynPixelsAddr.
    static_assert(BaseCount == kStringBaseCount, "kBindDynBase counts from the strings[] table");
    // Each row names its button and what it does, so a player who never
    // held an N64 finds "Zoom" without reading eighteen help lines, and a
    // player the game has just told to "Press Z Button" finds Z. The colon
    // is the port's own glyph; the widths were checked against the
    // harvested font (the widest, "L Button: Unused", is 93 pixels of the
    // 108 the label column has before the values at x=163).
    static const char* const bindLabels[BindInputCount] = {
        "A Button: Photo",
        "B Button: Ball",
        "Z Button: Zoom",
        "Start: Pause",
        "L Button: Unused",
        "R Button: Dash",
        "C-Up: Look Back",
        "C-Down: Flute",
        "C-Left: Turn",
        "C-Right: Turn",
        "D-Pad Up",
        "D-Pad Down",
        "D-Pad Left",
        "D-Pad Right",
        "Stick Up: Aim",
        "Stick Down: Aim",
        "Stick Left: Aim",
        "Stick Right: Aim",
    };
    // Within the help face's letters (no E F G I K Q U V X Y, no digits
    // but 2 3 4 6, no hyphen, apostrophe or colon), 41 characters a line.
    // Seven lines for the page itself (ids +125..+131): the Device row,
    // Restore Defaults asking, the row while it listens, Restore Defaults
    // at rest, a refused key, a refused clear, a listen that timed out.
    static const char* const bindDescs[7][2] = {
        { "Left and Right pick what to set up.",      "B goes back with every change kept." },
        { "Press A again to put this device back", "the way it came, B to keep it as it is." },
        { "Press the key or button to use for this.", "Wait a few seconds to leave it as it was." },
        { "Puts every row back the way the game came", "for the device at the top. A asks first." },
        { "That one already does something else.", "Choose another key or button." },
        { "Something must still press this button.", "Set another device before clearing this." },
        { "Nothing was pressed in time. The row", "stays as it was." },
    };
    // What each input does in the game, one line per row (ids +132..+149),
    // with what A and Z do on the second. L is honest: the cartridge never
    // reads it (nothing in the decompilation tests L_TRIG outside the
    // crash screen); the dash needs the Dash Engine.
    static const char* const bindInputDescs[BindInputCount][2] = {
        { "Zoomed in it takes the photo, zoomed out", "an apple. A changes it, Z clears it." },
        { "Throws a pester ball to wake or move a", "Pokemon. A changes it, Z clears it." },
        { "Zooms in for a photo, held down or as a", "switch. A changes it, Z clears it." },
        { "Pauses the ride, and starts the game on", "the title. A changes it, Z clears it." },
        { "Does nothing here, the game never reads", "L. A changes it, Z clears it." },
        { "Makes the cart dash while held, once you", "own the dash engine. A changes, Z clears." },
        { "Turns around to face behind the cart.", "A changes it, Z clears it." },
        { "Plays the flute, once you have it.", "A changes it, Z clears it." },
        { "Turns the camera to face left.", "A changes it, Z clears it." },
        { "Turns the camera to face right.", "A changes it, Z clears it." },
        { "Aims up in a course like the stick,", "and walks the menus. A changes, Z clears." },
        { "Aims down in a course like the stick,", "and walks the menus. A changes, Z clears." },
        { "Aims left in a course like the stick,", "and walks the menus. A changes, Z clears." },
        { "Aims right in a course like the stick,", "and walks the menus. A changes, Z clears." },
        { "Aims the camera up in a course, and", "walks the menus. A changes, Z clears." },
        { "Aims the camera down in a course, and", "walks the menus. A changes, Z clears." },
        { "Aims the camera left in a course, and", "walks the menus. A changes, Z clears." },
        { "Aims the camera right in a course, and", "walks the menus. A changes, Z clears." },
    };
    constexpr uint32_t StringCount = BaseCount + 279;

    const char* overrideNames[] = {
        nullptr, "graphics", "render_scale", "anti_aliasing", "widescreen",
        "frame_rate", "2d_detail", "filter", "dither", "fullscreen",
        "item_help", "auto", "off", "on",
        "1x", "2x", "3x", "4x", "5x", "6x", "7x", "8x",
        "original", "display", "classic", "sharp", "point", "smooth", "crisp",
        "page_help",
    };

    // The wordmark: any resolution, box-scaled to 128 texels wide (two
    // chunks), colours un-premultiplied back out of the average so edges
    // keep their hue, alpha cut at half for RGBA16's single bit.
    struct { int w = 0, h = 0; std::vector<uint16_t> texels; } logo;
    {
        int lw = 0, lh = 0, comp = 0;
        stbi_uc* data = load_png(asset_path("menu_text/recomp_logo.png"), &lw, &lh, &comp);
        if ((data != nullptr) && (lw > 0) && (lh > 0)) {
            // Image generators fight transparency: a fully opaque image is
            // treated as white-backgrounded, and near-white pixels become
            // the transparency. The wordmark's own colours are saturated,
            // so nothing of it gets keyed away.
            bool opaqueImg = true;
            for (int i = 0; i < lw * lh; i++) {
                if (data[size_t(i) * 4 + 3] < 250) {
                    opaqueImg = false;
                    break;
                }
            }
            if (opaqueImg) {
                for (int i = 0; i < lw * lh; i++) {
                    stbi_uc* px = data + size_t(i) * 4;
                    const int whiteness = std::min(px[0], std::min(px[1], px[2]));
                    if (whiteness >= 240) {
                        px[3] = 0;
                    }
                }
            }
            // Drawn size versus staged size: a 64-texel 16-bit chunk row can
            // carry at most 32 rows through a block load (2048 texels of
            // TMEM), so the visible mark scales to 92 wide -- proportioned
            // under the Snap logo -- inside a two-chunk buffer whose spare
            // columns stay transparent.
            const int visW = 92;
            const int visH = std::clamp(int(double(lh) * visW / lw + 0.5), 8, 32);
            const int outW = 128;
            logo.w = outW;
            logo.h = visH;
            logo.texels.assign(size_t(outW) * visH, 0);
            for (int y = 0; y < visH; y++) {
                const int sy0 = y * lh / visH;
                const int sy1 = std::max(sy0 + 1, (y + 1) * lh / visH);
                for (int x = 0; x < visW; x++) {
                    const int sx0 = x * lw / visW;
                    const int sx1 = std::max(sx0 + 1, (x + 1) * lw / visW);
                    uint32_t r = 0, g = 0, b = 0, a = 0, n = 0;
                    for (int sy = sy0; sy < sy1; sy++) {
                        for (int sx = sx0; sx < sx1; sx++) {
                            const stbi_uc* px = data + (size_t(sy) * lw + sx) * 4;
                            r += px[0] * px[3] / 255;
                            g += px[1] * px[3] / 255;
                            b += px[2] * px[3] / 255;
                            a += px[3];
                            n++;
                        }
                    }
                    a /= n;
                    if (a >= 128) {
                        r = std::min(255u, r / n * 255 / a);
                        g = std::min(255u, g / n * 255 / a);
                        b = std::min(255u, b / n * 255 / a);
                        logo.texels[size_t(y) * outW + x] =
                            uint16_t(((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | 1);
                    }
                }
            }
            stbi_image_free(data);
            printf("[SNAP-MENU] recomp_logo.png staged at %dx%d\n", logo.w, logo.h);
        }
    }

    // The pause menu's fourth pill, plain and selected (pause_menu_patch.inc).
    Rgba16Art pillPlain;
    Rgba16Art pillSel;
    const bool pills = compose_pause_pill(false, pillPlain) && compose_pause_pill(true, pillSel);
    if (!pills) {
        printf("[SNAP-MENU] the pause menu's pill was not composed (ROM %zu bytes); the pause menu keeps its three items\n",
               recomp::get_rom().size());
    }

    uint32_t cursor = PixelsAddr;
    write_u32(DirectoryAddr + 0x4, StringCount);

    for (uint32_t id = 0; id < StringCount; id++) {
        int w, h;
        Strip strip;
        if (id == 0) {
            // The backdrop: a solid black tile the patch stretches over the
            // whole screen under the page.
            w = 16;
            h = 16;
        }
        else if (id == BaseCount + 23) {
            strip = compose_credits(SNAP_PORT_CREDITS);
            apply_outline(strip);
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 22) {
            // The wordmark, staged even when absent: a zero width tells the
            // patch there is nothing to draw.
            w = logo.w;
            h = logo.h;
        }
        else if ((id == BaseCount + 24) || (id == BaseCount + 25)) {
            // The Graphics page's scroll arrows, up then down.
            strip = compose_scroll_arrow(id == BaseCount + 24);
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 26) {
            // The SOUND page's heading, in the header face.
            strip = compose_hdr("Sound");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 52) {
            strip = compose("Overscan Crop");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 53) {
            strip = compose_lines("Hides the picture edges a CRT cut off.",
                                  "Off shows every pixel the game draws.");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 54) {
            // No capital I in the body face (see STR_INTRO_LABEL in the
            // patch): the row is named for what the player sees.
            strip = compose("Cutscene Fix");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 55) {
            strip = compose_lines("Skips the clipped frame the console drew",
                                  "as a course intro hands off the camera.");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 56) {
            strip = compose("Photo Detail");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 57) {
            // No apostrophe in the help face, so not "Oak's photos".
            strip = compose_lines("Oak and the album show your photos at",
                                  "full resolution, not console pixels.");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 58) {
            // No capital J in the body face (see STR_JYNX_LABEL in the
            // patch): named for the releases whose look it borrows.
            strip = compose("Jynx Recolor");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 59) {
            // The help face has no J, no V and no hyphen: the description
            // says what turns purple and where to see it (Jynx dance in
            // the Cave), not the name, "VC" or "re-release".
            strip = compose_lines("Off keeps the black Jynx of the cartridge.",
                                  "On, the purple Jynx of the Wii and Switch.");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 60) {
            strip = compose_title("Snap Station");
            w = strip.width;
            h = (w > 0) ? strip.height : 0;
        }
        else if (id == BaseCount + 61) {
            // The CONTROLS page's heading; its C and l are the port's own
            // header glyphs (menu_harvest.cpp kHeaderSynth).
            strip = compose_hdr("Controls");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 62) {
            strip = add_item_dot(compose("Controls"));
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 63) {
            strip = compose_help("Mouse, controller and button settings.");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 96) {
            // The Option list's sixth item. Every letter of "Exit Game" is
            // in the body face; "Quit" is not (no Q).
            strip = add_item_dot(compose("Exit Game"));
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 97) {
            strip = compose_help("Closes the game.");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 98) {
            // What the help line becomes once the item is chosen; the
            // next A closes the program, B withdraws.
            strip = compose_help("Press A again to close the game, B to stay.");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 198) {
            strip = compose("Fast Forward");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 199) {
            strip = compose_lines(fastDesc[0], fastDesc[1]);
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 200) {
            strip = compose("Slow Motion");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 201) {
            strip = compose_lines(slowDesc[0], slowDesc[1]);
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 233) {
            // No V in the help face (its letters are the stock sentences' and
            // a few of the port's); the line begins with a word it has.
            strip = compose_help("The volumes, stereo or mono, background mute.");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 232) {
            // The in-course list's Sound item; the Option screen's is the
            // stock sprite (graphics_menu_patch.c, snap_course_options_page).
            strip = add_item_dot(compose("Sound"));
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 231) {
            // The in-course list's heading: the screen's own "Options"
            // sprite, texel for texel (the header face was cut from it),
            // which the patch places where the screen places it, 43,40.
            // A composition from the face runs one column wider.
            const MenuBitmap& word = g_font.hdrWord;
            if (word.w > 0) {
                strip = Strip();
                strip.width = (word.w + 63) & ~63;
                strip.height = word.h;
                strip.intensity.assign(size_t(strip.width) * strip.height, 0);
                strip.alpha.assign(size_t(strip.width) * strip.height, 0);
                for (int y = 0; y < word.h; y++) {
                    for (int x = 0; x < word.w; x++) {
                        strip.intensity[size_t(y) * strip.width + x] = word.ia[(size_t(y) * word.w + x) * 2 + 0];
                        strip.alpha[size_t(y) * strip.width + x] = word.ia[(size_t(y) * word.w + x) * 2 + 1];
                    }
                }
            }
            else {
                strip = compose_hdr("Options");
            }
            w = strip.width;
            h = strip.height;
        }
        else if ((id >= kOptDynBase) && (id < kOptDynBase + uint32_t(2 * OptBank))) {
            // A row of a mod's options page: a fixed home of its own, blank
            // until the page opens (opt_compose writes it then).
            const uint32_t slot = (id - kOptDynBase) % uint32_t(OptBank);
            const bool help = slot >= uint32_t(2 * OptVisible);
            const uint32_t addr = opt_dyn_addr(id);
            const uint32_t bytes = help ? ModsHelpBytes : DynStripBytes;
            write_u32(DirectoryAddr + 0x8 + id * 8, addr);
            write_u16(DirectoryAddr + 0xC + id * 8, 64);
            write_u16(DirectoryAddr + 0xE + id * 8, uint16_t(help ? ModsHelpHeight : StripHeight));
            for (uint32_t k = 0; k < bytes; k += 2) {
                write_u16(addr + k, 0);
            }
            continue;
        }
        else if (id == BaseCount + 274) {
            // The Mods page's hint at the header's right, in its three
            // forms: both of the page's extra buttons apply, only Z, only
            // the order.
            strip = compose("Z options   L R order");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 275) {
            strip = compose("Z opens its options");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 276) {
            strip = compose("L and R move it");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 277) {
            strip = compose_hdr("Mod Options");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 278) {
            strip = compose_lines("This mod has no options.", "");
            w = strip.width;
            h = strip.height;
        }
        else if ((id == BaseCount + 230) || (id == BaseCount + 234)) {
            // The pause menu's OPTIONS pill, plain and selected
            // (pause_menu_patch.inc): RGBA16, the pills' own format, and
            // drawn by the patch as such -- so without the composition
            // there is no entry, and the pause menu keeps the cartridge's
            // three items.
            if (!pills) {
                write_u32(DirectoryAddr + 0x8 + id * 8, 0);
                write_u16(DirectoryAddr + 0xC + id * 8, 0);
                write_u16(DirectoryAddr + 0xE + id * 8, 0);
                continue;
            }
            const Rgba16Art& art = (id == BaseCount + 230) ? pillPlain : pillSel;
            w = art.w;
            h = art.h;
        }
        else if (id == BaseCount + 235) {
            // The Option screen's rule, for the pages over a course.
            strip = compose_rule();
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 236) {
            // The help box's side, likewise.
            strip = compose_box_side();
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 237) {
            // The header's "A OK  B Cancel" as the game keeps it: 117x11,
            // 32-bit RGBA, its words' antialiasing in 8-bit alpha, in two
            // 64-texel blocks of 32-bit texels (704 texels a block, within
            // a 32-bit block load); the patch builds this one strip with
            // 32-bit bitmaps (snap_make_strip_32). Not harvested: no entry.
            const MenuArt& art = g_font.legend;
            const int lw = (art.w > 0) ? ((art.w + 63) & ~63) : 0;
            write_u32(DirectoryAddr + 0x8 + id * 8, (lw > 0) ? cursor : 0u);
            write_u16(DirectoryAddr + 0xC + id * 8, uint16_t(lw));
            write_u16(DirectoryAddr + 0xE + id * 8, uint16_t((lw > 0) ? art.h : 0));
            uint32_t at = cursor;
            for (int k = 0; k < lw / 64; k++) {
                for (int y = 0; y < art.h; y++) {
                    for (int x = 0; x < 64; x++) {
                        const int sx = k * 64 + x;
                        const uint32_t v = (sx < art.w) ? art.rgba[size_t(y) * size_t(art.w) + size_t(sx)] : 0u;
                        write_u32(at + uint32_t((y * 64 + x) * 4), v);
                    }
                }
                at += uint32_t(64 * art.h * 4);
            }
            cursor = (at + 7u) & ~7u;
            continue;
        }
        else if (id >= kModsDynBase) {
            // A row of the Mods page: a fixed home of its own, blank until
            // the page opens (mods_compose writes it and the directory
            // entry's width then).
            const bool help = ((id - kModsDynBase) % uint32_t(ModsBank)) >= uint32_t(ModsVisible);
            const uint32_t addr = mods_dyn_addr(id);
            const uint32_t bytes = help ? ModsHelpBytes : DynStripBytes;
            write_u32(DirectoryAddr + 0x8 + id * 8, addr);
            write_u16(DirectoryAddr + 0xC + id * 8, 64);
            write_u16(DirectoryAddr + 0xE + id * 8, uint16_t(help ? ModsHelpHeight : StripHeight));
            for (uint32_t k = 0; k < bytes; k += 2) {
                write_u16(addr + k, 0);
            }
            continue;
        }
        else if (id == BaseCount + 205) {
            strip = compose_lines("No mods were found in the mods folder.",
                                  "Put a mod there and start the game again.");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 204) {
            // The page's heading, in the header face; its M and d are the
            // port's (menu_harvest.cpp kHeaderSynth).
            strip = compose_hdr("Mods");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 203) {
            strip = compose_help("The mods in the mods folder, on or off.");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 202) {
            strip = add_item_dot(compose("Mods"));
            w = strip.width;
            h = strip.height;
        }
        else if (id >= kBindDynBase) {
            // A live row value of the Button Setup page: a fixed home of
            // its own, blank until the page opens (bind_compose writes it
            // and the directory entry's width then).
            const uint32_t addr = DynPixelsAddr + (id - kBindDynBase) * DynStripBytes;
            write_u32(DirectoryAddr + 0x8 + id * 8, addr);
            write_u16(DirectoryAddr + 0xC + id * 8, 64);
            write_u16(DirectoryAddr + 0xE + id * 8, uint16_t(StripHeight));
            for (uint32_t k = 0; k < DynStripBytes; k += 2) {
                write_u16(addr + k, 0);
            }
            continue;
        }
        else if (id >= BaseCount + 158) {
            strip = compose((id == BaseCount + 158) ? "< 120 >" : (id == BaseCount + 159) ? "< 144 >"
                          : (id == BaseCount + 160) ? "< 165 >" : "< 240 >");
            w = strip.width;
            h = strip.height;
        }
        else if (id >= BaseCount + 155) {
            strip = compose((id == BaseCount + 155) ? "< 5 >" : (id == BaseCount + 156) ? "< 15 >" : "< 35 >");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 154) {
            strip = compose_lines(deadzoneDesc[0], deadzoneDesc[1]);
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 153) {
            strip = compose("Dead Zone");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 152) {
            strip = compose_lines(sticksDesc[0], sticksDesc[1]);
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 151) {
            strip = compose("< Swapped >");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 150) {
            strip = compose("Pad Sticks");
            w = strip.width;
            h = strip.height;
        }
        else if (id >= BaseCount + 132) {
            strip = compose_lines(bindInputDescs[id - BaseCount - 132][0], bindInputDescs[id - BaseCount - 132][1]);
            w = strip.width;
            h = strip.height;
        }
        else if (id >= BaseCount + 125) {
            strip = compose_lines(bindDescs[id - BaseCount - 125][0], bindDescs[id - BaseCount - 125][1]);
            w = strip.width;
            h = strip.height;
        }
        else if (id >= BaseCount + 122) {
            // The Device row's values; the K is one of the port's own glyphs.
            strip = compose((id == BaseCount + 122) ? "< Keyboard >"
                          : (id == BaseCount + 123) ? "< Mouse >" : "< Controller >");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 121) {
            strip = compose("Restore Defaults");
            w = strip.width;
            h = strip.height;
        }
        else if (id >= BaseCount + 103) {
            strip = compose(bindLabels[id - BaseCount - 103]);
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 102) {
            // The top row picks whose buttons the page sets up; "Set Up:"
            // says what the row does where "Device" only said what it was.
            strip = compose("Set Up:");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 101) {
            // The page's heading, in the game's own words for such a
            // screen ("Z Button Setup"); its B and e are the port's
            // (menu_harvest.cpp kHeaderSynth).
            strip = compose_hdr("Button Setup");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 100) {
            // The CONTROLS page's row that opens the page: no value, as the
            // Option list's own Screen row has none, and the help line
            // says what A does.
            strip = compose_lines("Press A to choose what each key, mouse",
                                  "button and pad button does.");
            w = strip.width;
            h = strip.height;
        }
        else if (id == BaseCount + 99) {
            strip = compose("Button Setup");
            w = strip.width;
            h = strip.height;
        }
        else if (id >= BaseCount + 94) {
            strip = compose_lines(gyroDescs[id - BaseCount - 94][0], gyroDescs[id - BaseCount - 94][1]);
            w = strip.width;
            h = strip.height;
        }
        else if (id >= BaseCount + 91) {
            strip = compose(gyroStrings[id - BaseCount - 91]);
            w = strip.width;
            h = strip.height;
        }
        else if (id >= BaseCount + 85) {
            strip = compose_lines(ctlDescs[id - BaseCount - 85][0], ctlDescs[id - BaseCount - 85][1]);
            w = strip.width;
            h = strip.height;
        }
        else if (id >= BaseCount + 64) {
            strip = compose(ctlStrings[id - BaseCount - 64]);
            w = strip.width;
            h = strip.height;
        }
        else if (id >= BaseCount + 46) {
            // The SOUND page's setting descriptions.
            strip = compose_lines(sndDescs[id - BaseCount - 46][0], sndDescs[id - BaseCount - 46][1]);
            w = strip.width;
            h = strip.height;
        }
        else if (id >= BaseCount + 27) {
            // The SOUND page's labels and values, in the body face.
            strip = compose(sndStrings[id - BaseCount - 27]);
            w = strip.width;
            h = strip.height;
        }
        else if (id >= BaseCount + 18) {
            // Second-wave setting descriptions. No outline: the stock help
            // sprites are plain white IA texels whose only edge is their own
            // antialiased fringe -- a baked ring here reads bolder than any
            // stock line in the menu.
            strip = compose_lines(extraDescs[id - BaseCount - 18][0], extraDescs[id - BaseCount - 18][1]);
            w = strip.width;
            h = strip.height;
        }
        else if (id >= BaseCount + 9) {
            // Second-wave labels and values.
            strip = compose(extraStrings[id - BaseCount - 9]);
            w = strip.width;
            h = strip.height;
        }
        else if (id >= BaseCount + 8) {
            // The page heading, in the header face.
            strip = compose_hdr("Graphics");
            w = strip.width;
            h = strip.height;
        }
        else if (id >= BaseCount) {
            // A two-line setting description for the help box; borderless
            // like every stock help sprite.
            strip = compose_lines(descs[id - BaseCount][0], descs[id - BaseCount][1]);
            w = strip.width;
            h = strip.height;
        }
        else if ((overrideNames[id] != nullptr) && load_override(overrideNames[id], strip)) {
            // A hand-made image takes the string's place.
            w = strip.width;
            h = strip.height;
        }
        else if ((id == 10) || (id == 29)) {
            // The one-line help sentences render in the help face, soft
            // edges and all, like the stock sentences sharing the box.
            strip = compose_help(strings[id]);
            w = strip.width;
            h = strip.height;
        }
        else {
            strip = compose(strings[id]);
            if (id == STR_ITEM_LABEL_ID) {
                // The list item carries the stock items' bullet dot, taken
                // from the original sprite, at the original spacing.
                strip = add_item_dot(strip);
            }
            w = strip.width;
            h = strip.height;
        }

        write_u32(DirectoryAddr + 0x8 + id * 8, cursor);
        write_u16(DirectoryAddr + 0xC + id * 8, uint16_t(w));
        write_u16(DirectoryAddr + 0xE + id * 8, uint16_t(h));

        if ((id == BaseCount + 23) || (id == BaseCount + 24) || (id == BaseCount + 25)) {
            RainbowStrip& rs = (id == BaseCount + 23) ? g_credits
                                                      : g_arrows[id - BaseCount - 24];
            rs.addr = cursor;
            rs.w = w;
            rs.h = h;
            // Cores only: the animator must never touch the black border.
            rs.mask.assign(size_t(w) * h, 0);
            for (size_t px = 0; px < rs.mask.size(); px++) {
                if ((strip.alpha[px] >= 128) && (strip.intensity[px] >= 128)) {
                    rs.mask[px] = 255;
                }
            }
        }

        // Written as contiguous 64-texel column blocks, because the sprite
        // library loads each bitmap with a block load that cannot stride
        // through a wider image. Chunk k covers columns [64k, 64k+cw).
        const int chunks = (w + 63) / 64;
        // A strip slot carries six chunks, 384px, on both the build and the
        // swap path (SNAP_STRIP_CHUNKS in graphics_menu_patch.c). This was a
        // warning while the old allocator sized itself from the string and
        // simply grew to fit; the pool cannot grow, so anything past the slot
        // is cut off on screen with no other symptom. It is an error now.
        // The string is still staged, clamped: the directory entry for this
        // id was written a few lines above, so skipping it here would leave
        // that entry pointing at pixels nobody staged, which is worse than a
        // truncated line.
        if (chunks > 6) {
            printf("[SNAP-GFX] ERROR: string %d is %dpx wide (%d chunks); a strip carries "
                   "six (384px), so it WILL be cut off. Shorten the string, or raise "
                   "SNAP_STRIP_CHUNKS and SNAP_STRIP_STRIDE together in "
                   "graphics_menu_patch.c.\n",
                   id, w, chunks);
            fflush(stdout);
        }
        uint32_t at = cursor;
        for (int k = 0; k < chunks; k++) {
            const int cx = k * 64;
            const int cw = std::min(64, w - cx);
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < cw; x++) {
                    uint16_t texel;
                    if (id == 0) {
                        texel = 0x00FF;   // black, opaque
                    }
                    else if (id == BaseCount + 22) {
                        texel = logo.texels[size_t(y) * w + (cx + x)];   // RGBA16
                    }
                    else if ((id == BaseCount + 230) || (id == BaseCount + 234)) {
                        texel = ((id == BaseCount + 230) ? pillPlain : pillSel).texels[size_t(y) * w + (cx + x)];   // RGBA16
                    }
                    else if ((id == BaseCount + 23) || (id == BaseCount + 24) ||
                             (id == BaseCount + 25)) {
                        // Cores start white and are recoloured live by the
                        // rainbow animator; the border stays opaque black.
                        const size_t src = size_t(y) * w + (cx + x);
                        if (strip.alpha[src] >= 128) {
                            texel = (strip.intensity[src] >= 128) ? 0xFFFF : 0x0001;
                        } else {
                            texel = 0;
                        }
                    }
                    else {
                        const size_t src = size_t(y) * w + (cx + x);
                        texel = uint16_t((strip.intensity[src] << 8) | strip.alpha[src]);
                    }
                    write_u16(at + uint32_t((y * cw + x) * 2), texel);
                }
            }
            at += uint32_t(cw * h * 2);
        }
        cursor = at;
        cursor = (cursor + 7u) & ~7u;
    }

    if (g_missing_glyphs != 0) {
        // Withheld: no magic, so the patch draws nothing of ours and the
        // next main-menu load tries again. The animators must not paint
        // into strips nobody will read.
        printf("[SNAP-MENU] %d characters have no glyph in the harvested faces; the staged strings are withheld\n",
               g_missing_glyphs);
        g_credits = RainbowStrip{};
        g_arrows[0] = RainbowStrip{};
        g_arrows[1] = RainbowStrip{};
        return;
    }
    write_u32(DirectoryAddr + 0x0, DirectoryMagic);
    g_staged = true;
    printf("[SNAP-MENU] staged %u interface strings (%u bytes) for the graphics page\n",
        StringCount, cursor - PixelsAddr);
}

// Writes a strip into a dynamic id's fixed pixels and its directory entry
// (the address never moves; the width and height follow the text, within
// what the slot holds).
static void stage_dynamic_at(uint32_t id, uint32_t addr, const Strip& strip, int maxChunks, int maxHeight) {
    const int w = std::min(strip.width, maxChunks * 64);
    const int h = std::min(strip.height, maxHeight);
    uint32_t at = addr;
    for (int k = 0; k < (w + 63) / 64; k++) {
        const int cx = k * 64;
        const int cw = std::min(64, w - cx);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < cw; x++) {
                const size_t src = size_t(y) * strip.width + size_t(cx + x);
                write_u16(at + uint32_t((y * cw + x) * 2),
                          uint16_t((strip.intensity[src] << 8) | strip.alpha[src]));
            }
        }
        at += uint32_t(cw * h * 2);
    }
    write_u32(DirectoryAddr + 0x8 + id * 8, addr);
    write_u16(DirectoryAddr + 0xC + id * 8, uint16_t(w));
    write_u16(DirectoryAddr + 0xE + id * 8, uint16_t(h));
}

// A live row value of the Button Setup page.
static void stage_dynamic(uint32_t id, const Strip& strip) {
    stage_dynamic_at(id, DynPixelsAddr + (id - kBindDynBase) * DynStripBytes, strip, DynChunks, StripHeight);
}

// Composes the eighteen row values for a device into the bank the page is
// not showing, then turns the page to it: the bank shown is BIND_GEN's low
// bit, so the one written is always the other.
static void bind_compose(int device) {
    const uint32_t gen = read_u32_mail(BindGenAddr);
    const uint32_t bank = (gen + 1) & 1;
    for (int i = 0; i < BindInputCount; i++) {
        stage_dynamic(kBindDynBase + bank * BindInputCount + uint32_t(i),
                      compose_dyn(input_bind_display(kBindInputs[i], device)));
    }
    write_u32(BindGenAddr, gen + 1);
    g_bind_shown_device = device;
    g_bind_shown_gen = input_bindings_generation();
}

// The BUTTON SETUP page's bank, once a tick. A request is answered when it is
// done: a capture keeps listening across ticks until the input layer says
// how it ended (input.h); a clear and the reset are immediate. The row
// values follow the device shown and the table in force, recomposed ahead
// of the answer so the page finds them there the frame it wakes.
static void poll_bind_bank() {
    write_u8(BindPadAddr, input_pad_attached() ? 1 : 0);
    if (read_u8_mail(BindOpenAddr) == 0) {
        if (g_bind_capturing) {
            input_capture_end();
            g_bind_capturing = false;
        }
        g_bind_handled = 0;
        g_bind_shown_device = -1;
        return;
    }
    const int device = std::min<int>(read_u8_mail(BindDeviceAddr), 2);
    const uint32_t req = read_u32_mail(BindReqAddr);
    uint32_t ack = 0;
    if (req == 0) {
        if (g_bind_capturing) {
            // The page gave up waiting.
            input_capture_end();
            g_bind_capturing = false;
        }
        g_bind_handled = 0;
    }
    else if (req != g_bind_handled) {
        const int op = int((req >> 16) & 0xFF);
        const int reqDevice = int((req >> 8) & 0xFF);
        const int row = int(req & 0xFF);
        const char* input = ((row >= 1) && (row <= BindInputCount)) ? kBindInputs[row - 1] : nullptr;
        uint32_t result = 0;
        if ((op == 1) && (input != nullptr) && (reqDevice <= 2)) {
            if (!g_bind_capturing) {
                input_capture_begin(reqDevice);
                g_bind_capturing = true;
            }
            std::string name;
            const CaptureState st = input_capture_poll(&name);
            if (st != CaptureState::Listening) {
                input_capture_end();
                g_bind_capturing = false;
                if (st == CaptureState::Bound) {
                    input_bind_set(input, reqDevice, name);
                    result = 1;
                }
                else if (st == CaptureState::Cancelled) {
                    result = 2;
                    printf("[SNAP-Input] Button Setup page: the listen was cancelled with Esc; the row stays as it was\n");
                    fflush(stdout);
                }
                else if (st == CaptureState::TimedOut) {
                    result = 5;
                }
                else {
                    result = 3;
                    printf("[SNAP-Input] Button Setup page: \"%s\" refused, it has a job of its own; the row stays as it was\n", name.c_str());
                    fflush(stdout);
                }
            }
        }
        else if ((op == 2) && (input != nullptr) && (reqDevice <= 2)) {
            result = input_bind_clear(input, reqDevice) ? 1 : 4;
        }
        else if ((op == 3) && (reqDevice <= 2)) {
            input_bind_reset(reqDevice);
            result = 1;
        }
        else {
            result = 2;
        }
        if (result != 0) {
            ack = (result << 24) | req;
            g_bind_handled = req;
        }
    }
    if ((device != g_bind_shown_device) || (input_bindings_generation() != g_bind_shown_gen)) {
        bind_compose(device);
    }
    if (ack != 0) {
        write_u32(BindAckAddr, ack);
    }
}


// ---------------------------------------------------------------------------
// The MODS page's bank (graphics_menu_patch.c, snap_mods_page), once a tick.
//
// The list is taken from the runtime when the page opens: every mod the
// folder holds for this game, in the runtime's order, with what mods.json
// says of each. The window the page shows (MODS_TOP) is composed into the
// bank not on show -- the name cut to the label column, a help line of the
// mod's own short description over the port's sentence on when a change
// takes effect -- and the generation turned; a toggle goes to the runtime,
// which writes mods.json at once, and the window is recomposed ahead of the
// answer so the row's value and line are new the frame the page wakes.
// ---------------------------------------------------------------------------
struct ModsRow {
    std::string id;
    std::string name;      // the display name, and its version after it
    std::string desc;
    std::string author;    // the first author, for the help line
    std::string needs;     // an unmet dependency, said in the help line
    int optCount = 0;      // the options its manifest declares, hidden ones aside
    bool enabled = false;
    bool live = false;   // runtime-toggleable content, in force at once
};
static std::vector<ModsRow> g_mods_rows;
static int g_mods_shown_top = -1;
static uint32_t g_mods_handled = 0;


// The text a mod wrote, made safe for a face: a character the face has no
// glyph for is left out of a help line (compose_help would count it and the
// next staging would withhold every string for it) and left to the body
// face's stand-in block in a name; line breaks and tabs become spaces.
static std::string face_text(const std::string& text, bool help) {
    std::string out;
    for (char c : text) {
        if ((c == '\n') || (c == '\r') || (c == '\t')) {
            if (!out.empty() && (out.back() != ' ')) {
                out += ' ';
            }
        }
        else if (c == ' ') {
            out += c;
        }
        else if (!help) {
            out += c;
        }
        else if ((c > 0x20) && (c < 0x7F) && (g_font.hlp.find(c) != nullptr)) {
            out += c;
        }
    }
    while (!out.empty() && (out.back() == ' ')) {
        out.pop_back();
    }
    return out;
}

// The rightmost column with any ink, for a fit the rounded strip width
// cannot give.
static int ink_width(const Strip& strip) {
    int right = 0;
    for (int y = 0; y < strip.height; y++) {
        for (int x = strip.width - 1; x >= right; x--) {
            if (strip.alpha[size_t(y) * strip.width + size_t(x)] != 0) {
                right = x + 1;
                break;
            }
        }
    }
    return right;
}

// Text cut to fit: character by character until the composed ink fits the
// width, then back to the last word break when one lies in the second half
// of what is left, so a cut reads as a cut and not as a misspelling.
static std::string cut_to_fit(const std::string& text, int maxInk, const std::function<int(const std::string&)>& inkOf) {
    std::string cut = text;
    if (inkOf(cut) <= maxInk) {
        return cut;
    }
    while (!cut.empty() && (inkOf(cut) > maxInk)) {
        cut.pop_back();
    }
    const size_t space = cut.rfind(' ');
    if ((space != std::string::npos) && (space >= cut.size() / 2)) {
        cut.erase(space);
    }
    while (!cut.empty() && (cut.back() == ' ')) {
        cut.pop_back();
    }
    return cut;
}

// A mod's name in the label column, cut to end before the values.
static Strip compose_mod_name(const std::string& text) {
    const std::string cut = cut_to_fit(text, ModsNameInkWidth,
        [](const std::string& s) { return ink_width(compose(s.c_str(), true)); });
    return compose(cut.c_str(), true);
}

// A mod's help line: its short description over the state sentence, each
// cut to the help box's width.
static std::string cut_help(const std::string& text) {
    return cut_to_fit(text, ModsHelpInkWidth,
        [](const std::string& s) { return ink_width(compose_help(s.c_str())); });
}

static Strip compose_mod_help(const std::string& desc, const std::string& state) {
    const std::string cut1 = cut_help(desc);
    const std::string cut2 = cut_help(state);
    return compose_lines(cut1.c_str(), cut2.c_str());
}

// The state sentence, and who made the mod when the line has room for it.
static std::string mods_state_line(const ModsRow& row) {
    std::string line;
    if (!row.needs.empty()) {
        return row.needs;
    }
    if (row.live) {
        line = row.enabled ? "This mod is on. A turns it off." : "This mod is off. A turns it on.";
    } else {
        line = row.enabled ? "This mod is on. A turns it off at the next start."
                           : "This mod is off. A turns it on at the next start.";
    }
    if (!row.author.empty()) {
        line += " By " + row.author + ".";
    }
    return line;
}

// The two rows under the mods, as the other recompilations' mod menus have
// them as buttons.
static const char* const kModsActionNames[ModsActions] = { "Open the mods folder", "Restart the game" };
static const char* const kModsActionHelps[ModsActions][2] = {
    { "Shows the folder in the file browser. Drop a mod there,", "or on the window, and restart the game to load it." },
    { "Closes the game and starts it again, loading", "the mods as they are set here." },
};

// The runtime's list, in its order, and what mods.json says of each.
static void mods_gather() {
    g_mods_rows.clear();
    size_t index = 0;
    for (const recomp::mods::ModDetails& d : recomp::mods::get_all_mod_details("pokemonsnap")) {
        ModsRow row;
        row.id = d.mod_id;
        // The runtime's list runs in its own index order; a mod whose
        // required dependency is missing or the wrong version says so in
        // place of its state, as the other recompilations' menus flag it.
        if (recomp::mods::get_mod_id(index) == d.mod_id) {
            for (const recomp::mods::Dependency& dep : d.dependencies) {
                if (dep.optional) {
                    continue;
                }
                const recomp::mods::DependencyStatus st = recomp::mods::is_dependency_met(index, dep.mod_id);
                if (st == recomp::mods::DependencyStatus::NotFound) {
                    row.needs = face_text("Needs " + dep.mod_id + ", which is not in the folder.", true);
                    break;
                }
                if (st == recomp::mods::DependencyStatus::WrongVersion) {
                    row.needs = face_text("Needs another version of " + dep.mod_id + ".", true);
                    break;
                }
            }
        }
        index++;
        row.name = face_text(d.display_name.empty() ? d.mod_id : d.display_name, false);
        if ((d.version.major >= 0) && (d.version.minor >= 0) && (d.version.patch >= 0)) {
            char v[48];
            snprintf(v, sizeof(v), "  %d.%d.%d", d.version.major, d.version.minor, d.version.patch);
            row.name += v;
        }
        row.desc = face_text(d.short_description.empty() ? d.description : d.short_description, true);
        if (!d.authors.empty()) {
            row.author = face_text(d.authors[0], true);
        }
        row.enabled = recomp::mods::is_mod_enabled(d.mod_id);
        row.live = d.runtime_toggleable;
        for (const recomp::config::ConfigOption& o : recomp::mods::get_mod_config_schema(d.mod_id).options) {
            if (!o.hidden) {
                row.optCount++;
            }
        }
        g_mods_rows.push_back(row);
    }
    const size_t mods = std::min<size_t>(g_mods_rows.size(), 250 - ModsActions);
    write_u16(ModsModCountAddr, uint16_t(mods));
    write_u16(ModsCountAddr, uint16_t(mods + ModsActions));
    printf("[SNAP-MENU] Mods page: %zu mod%s in the folder\n", g_mods_rows.size(), (g_mods_rows.size() == 1) ? "" : "s");
    fflush(stdout);
}

// Composes the window at top into the bank the page is not showing, sets
// the bank's state byte, then turns the page to it.
static void mods_compose(int top) {
    const uint32_t gen = read_u32_mail(ModsGenAddr);
    const uint32_t bank = (gen + 1) & 1;
    const size_t mods = std::min<size_t>(g_mods_rows.size(), 250 - ModsActions);
    uint8_t state = 0;
    uint8_t hasOpt = 0;
    for (int i = 0; i < ModsVisible; i++) {
        const size_t row = size_t(top) + size_t(i);
        const uint32_t nameId = kModsDynBase + bank * uint32_t(ModsBank) + uint32_t(i);
        const uint32_t helpId = nameId + uint32_t(ModsVisible);
        Strip name;
        Strip help;
        if (row < mods) {
            const ModsRow& r = g_mods_rows[row];
            name = compose_mod_name(r.name);
            help = compose_mod_help(r.desc, mods_state_line(r));
            if (r.enabled) {
                state |= uint8_t(1u << i);
            }
            if (r.optCount > 0) {
                hasOpt |= uint8_t(1u << i);
            }
        }
        else if (row < mods + size_t(ModsActions)) {
            const size_t action = row - mods;
            name = compose(kModsActionNames[action], true);
            help = compose_lines(kModsActionHelps[action][0], kModsActionHelps[action][1]);
        }
        else {
            name = compose("", true);
            help = compose_lines("", "");
        }
        stage_dynamic_at(nameId, mods_dyn_addr(nameId), name, DynChunks, StripHeight);
        stage_dynamic_at(helpId, mods_dyn_addr(helpId), help, ModsHelpChunks, ModsHelpHeight);
    }
    write_u8(ModsStateAddr + bank, state);
    write_u8(ModsHasOptAddr + bank, hasOpt);
    write_u32(ModsGenAddr, gen + 1);
    g_mods_shown_top = top;
}

static void poll_mods_bank() {
    if (read_u8_mail(ModsOpenAddr) == 0) {
        g_mods_shown_top = -1;
        g_mods_handled = 0;
        return;
    }
    if (g_mods_shown_top < 0) {
        mods_gather();
    }
    const int top = int(read_u8_mail(ModsTopAddr));
    const uint32_t req = read_u32_mail(ModsReqAddr);
    bool recompose = (top != g_mods_shown_top);
    uint32_t ack = 0;
    if (req == 0) {
        g_mods_handled = 0;
    }
    else if (req != g_mods_handled) {
        const int op = int((req >> 16) & 0xFF);
        const size_t row = size_t(req & 0xFFFF);
        uint32_t result = 3;
        if ((op == 1) && (row < g_mods_rows.size())) {
            ModsRow& r = g_mods_rows[row];
            recomp::mods::enable_mod(r.id, !r.enabled);
            r.enabled = recomp::mods::is_mod_enabled(r.id);
            result = r.enabled ? 1 : 2;
            recompose = true;
            printf("[SNAP-MENU] Mods page: %s is %s in mods.json%s\n", r.id.c_str(),
                   r.enabled ? "on" : "off", r.live ? "" : "; in force at the next start");
            fflush(stdout);
        }
        else if (((op == 2) || (op == 3)) && (row < g_mods_rows.size())) {
            // The mod one place up or down the load order, which the
            // runtime keeps in mods.json; the list is read again in the
            // new order.
            const std::string id = g_mods_rows[row].id;
            const size_t at = recomp::mods::get_mod_order_index(id);
            const size_t to = (op == 2) ? ((at > 0) ? at - 1 : 0) : at + 1;
            if (to != at) {
                recomp::mods::set_mod_index("pokemonsnap", id, to);
                mods_gather();
                result = 4;
                recompose = true;
                printf("[SNAP-MENU] Mods page: %s moved %s in the load order\n", id.c_str(), (op == 2) ? "up" : "down");
                fflush(stdout);
            }
        }
        else if (op == 4) {
            const std::filesystem::path dir = recomp::mods::get_mods_directory();
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            mods_open_folder(reinterpret_cast<const char*>(dir.u8string().c_str()));
            result = 4;
            printf("[SNAP-MENU] Mods page: opening the mods folder\n");
            fflush(stdout);
        }
        else if (op == 5) {
            printf("[SNAP-MENU] Mods page: restarting the game\n");
            fflush(stdout);
            mods_restart_game();
            result = 4;
        }
        g_mods_handled = req;
        ack = (result << 24) | req;
    }
    if (recompose) {
        mods_compose(top);
    }
    if (ack != 0) {
        write_u32(ModsAckAddr, ack);
    }
}

// ---------------------------------------------------------------------------
// A mod's options page. The runtime keeps each mod's options as its
// manifest declares them -- an enum, a number with a range and a step, a
// yes-or-no, a text -- and their values in the mod's own settings file,
// which the other recompilations' mod menus edit. The page shows the
// window of six the patch asks for, in the body face, and each Left or
// Right is written through set_mod_config_value at once.
// ---------------------------------------------------------------------------
struct OptRow {
    recomp::config::ConfigOption option;
    std::string name;
    std::string desc;
    std::string hint;   // what Left and Right do to it
};
static std::vector<OptRow> g_opt_rows;
static std::string g_opt_mod_id;
static int g_opt_shown_top = -1;
static uint32_t g_opt_handled = 0;

static std::string number_text(double v, int precision) {
    char buf[48];
    snprintf(buf, sizeof(buf), "%.*f", std::clamp(precision, 0, 6), v);
    return buf;
}

static std::string opt_value_text(const OptRow& r) {
    using namespace recomp::config;
    const ConfigValueVariant v = recomp::mods::get_mod_config_value(g_opt_mod_id, r.option.id);
    std::string text;
    switch (r.option.type) {
        case ConfigOptionType::Enum: {
            const ConfigOptionEnum& e = std::get<ConfigOptionEnum>(r.option.variant);
            const uint32_t val = std::holds_alternative<uint32_t>(v) ? std::get<uint32_t>(v) : e.default_value;
            const auto it = e.find_option_from_value(val);
            text = (it != e.options.end()) ? it->name : std::to_string(val);
            break;
        }
        case ConfigOptionType::Bool: {
            const ConfigOptionBool& b = std::get<ConfigOptionBool>(r.option.variant);
            const bool on = std::holds_alternative<bool>(v) ? std::get<bool>(v) : b.default_value;
            text = on ? "On" : "Off";
            break;
        }
        case ConfigOptionType::Number: {
            const ConfigOptionNumber& n = std::get<ConfigOptionNumber>(r.option.variant);
            const double d = std::holds_alternative<double>(v) ? std::get<double>(v) : n.default_value;
            text = number_text(d, n.precision);
            break;
        }
        case ConfigOptionType::String: {
            const ConfigOptionString& s = std::get<ConfigOptionString>(r.option.variant);
            text = std::holds_alternative<std::string>(v) ? std::get<std::string>(v) : s.default_value;
            break;
        }
        default:
            break;
    }
    text = face_text(text, false);
    return "< " + text + " >";
}

static void opt_gather(const std::string& mod_id) {
    using namespace recomp::config;
    g_opt_rows.clear();
    g_opt_mod_id = mod_id;
    for (const ConfigOption& o : recomp::mods::get_mod_config_schema(mod_id).options) {
        if (o.hidden) {
            continue;
        }
        OptRow row;
        row.option = o;
        row.name = face_text(o.name.empty() ? o.id : o.name, false);
        row.desc = face_text(o.description, true);
        switch (o.type) {
            case ConfigOptionType::Enum: {
                const size_t n = std::get<ConfigOptionEnum>(o.variant).options.size();
                row.hint = (n > 1) ? "Left and Right pick the next one." : "This one has a single choice.";
                break;
            }
            case ConfigOptionType::Bool:
                row.hint = "Left and Right turn it on or off.";
                break;
            case ConfigOptionType::Number:
                row.hint = "Left and Right change it a step at a time.";
                break;
            default:
                row.hint = "Text: set in the mod's own settings file.";
                break;
        }
        g_opt_rows.push_back(row);
    }
    write_u16(OptCountAddr, uint16_t(std::min<size_t>(g_opt_rows.size(), 250)));
    printf("[SNAP-MENU] Mod options: %s has %zu option%s\n", mod_id.c_str(), g_opt_rows.size(),
           (g_opt_rows.size() == 1) ? "" : "s");
    fflush(stdout);
}

static void opt_change(size_t row, int delta) {
    using namespace recomp::config;
    if (row >= g_opt_rows.size()) {
        return;
    }
    const OptRow& r = g_opt_rows[row];
    const ConfigValueVariant v = recomp::mods::get_mod_config_value(g_opt_mod_id, r.option.id);
    switch (r.option.type) {
        case ConfigOptionType::Enum: {
            const ConfigOptionEnum& e = std::get<ConfigOptionEnum>(r.option.variant);
            if (e.options.empty()) {
                return;
            }
            const uint32_t val = std::holds_alternative<uint32_t>(v) ? std::get<uint32_t>(v) : e.default_value;
            size_t idx = 0;
            for (size_t i = 0; i < e.options.size(); i++) {
                if (e.options[i].value == val) {
                    idx = i;
                    break;
                }
            }
            idx = (idx + e.options.size() + size_t((delta > 0) ? 1 : -1)) % e.options.size();
            recomp::mods::set_mod_config_value(g_opt_mod_id, r.option.id, ConfigValueVariant(e.options[idx].value));
            break;
        }
        case ConfigOptionType::Bool: {
            const ConfigOptionBool& b = std::get<ConfigOptionBool>(r.option.variant);
            const bool on = std::holds_alternative<bool>(v) ? std::get<bool>(v) : b.default_value;
            recomp::mods::set_mod_config_value(g_opt_mod_id, r.option.id, ConfigValueVariant(!on));
            break;
        }
        case ConfigOptionType::Number: {
            const ConfigOptionNumber& n = std::get<ConfigOptionNumber>(r.option.variant);
            double d = std::holds_alternative<double>(v) ? std::get<double>(v) : n.default_value;
            const double step = (n.step > 0.0) ? n.step : 1.0;
            d += (delta > 0) ? step : -step;
            if (n.max > n.min) {
                d = std::clamp(d, n.min, n.max);
            }
            const double scale = std::pow(10.0, std::clamp(n.precision, 0, 6));
            d = std::round(d * scale) / scale;
            recomp::mods::set_mod_config_value(g_opt_mod_id, r.option.id, ConfigValueVariant(d));
            break;
        }
        default:
            break;
    }
}

static void opt_compose(int top) {
    const uint32_t gen = read_u32_mail(OptGenAddr);
    const uint32_t bank = (gen + 1) & 1;
    for (int i = 0; i < OptVisible; i++) {
        const size_t row = size_t(top) + size_t(i);
        const uint32_t nameId = kOptDynBase + bank * uint32_t(OptBank) + uint32_t(i);
        const uint32_t valueId = nameId + uint32_t(OptVisible);
        const uint32_t helpId = nameId + uint32_t(2 * OptVisible);
        Strip name;
        Strip value;
        Strip help;
        if (row < g_opt_rows.size()) {
            const OptRow& r = g_opt_rows[row];
            const std::string nameCut = cut_to_fit(r.name, OptNameInkWidth,
                [](const std::string& s) { return ink_width(compose(s.c_str(), true)); });
            const std::string valueCut = cut_to_fit(opt_value_text(r), OptValueInkWidth,
                [](const std::string& s) { return ink_width(compose(s.c_str(), true)); });
            name = compose(nameCut.c_str(), true);
            value = compose(valueCut.c_str(), true);
            help = compose_mod_help(r.desc, r.hint);
        }
        else {
            name = compose("", true);
            value = compose("", true);
            help = compose_lines("", "");
        }
        stage_dynamic_at(nameId, opt_dyn_addr(nameId), name, DynChunks, StripHeight);
        stage_dynamic_at(valueId, opt_dyn_addr(valueId), value, DynChunks, StripHeight);
        stage_dynamic_at(helpId, opt_dyn_addr(helpId), help, ModsHelpChunks, ModsHelpHeight);
    }
    write_u32(OptGenAddr, gen + 1);
    g_opt_shown_top = top;
}

static void poll_opt_bank() {
    if (read_u8_mail(OptOpenAddr) == 0) {
        g_opt_shown_top = -1;
        g_opt_handled = 0;
        return;
    }
    if (g_opt_shown_top < 0) {
        const size_t modRow = read_u8_mail(OptRowAddr);
        if (modRow < g_mods_rows.size()) {
            opt_gather(g_mods_rows[modRow].id);
        } else {
            g_opt_rows.clear();
            write_u16(OptCountAddr, 0);
        }
    }
    const int top = int(read_u8_mail(OptTopAddr));
    const uint32_t req = read_u32_mail(OptReqAddr);
    bool recompose = (top != g_opt_shown_top);
    uint32_t ack = 0;
    if (req == 0) {
        g_opt_handled = 0;
    }
    else if (req != g_opt_handled) {
        const int op = int((req >> 16) & 0xFF);
        const int delta = int(int8_t((req >> 8) & 0xFF));
        const size_t row = size_t(req & 0xFF);
        uint32_t result = 3;
        if ((op == 6) && (row < g_opt_rows.size())) {
            opt_change(row, delta);
            result = 4;
            recompose = true;
        }
        g_opt_handled = req;
        ack = (result << 24) | req;
    }
    if (recompose) {
        opt_compose(top);
    }
    if (ack != 0) {
        write_u32(OptAckAddr, ack);
    }
}

// Called every game tick. Applies whatever the GRAPHICS page published
// since the last look -- the same live path the hotkeys use -- and marks
// the settings dirty. The disk write is not this thread's: a stick held on
// a slider bumps the sequence once per notch, and writing here made every
// notch a file write on the game tick. Mutations hold settings_mutex(); the
// apply calls read the struct after it is released, on this same thread.
void poll_menu_mailbox(uint8_t* rdram) {
    if ((rdram == nullptr) || !g_mailbox_seeded) {
        return;
    }
    g_menu_rdram = rdram;
    if (read_u32_mail(MailboxAddr) != MailboxMagic) {
        return;
    }
    // The renderer's horizontal widening, for the culling patch: a host-owned
    // word the page never writes, so it is simply rewritten every tick and
    // follows the page, the F10 hotkey and a window resize within a tick or
    // two of the renderer.
    write_u32(MailboxAddr + 0x44, view_wide_q8());
    // The culling patch's count of verdicts its wider bound changed, for a
    // replay to measure the patch by; printed as it grows, at most once
    // every five seconds of ticks, only under SNAP_STATS.
    if (snapdiag::statsEnabled()) {
        static uint32_t lastSaved = 0;
        static uint32_t ticksSince = 0;
        const uint32_t saved = read_u32_mail(MailboxAddr + 0x48);
        if ((saved != lastSaved) && (++ticksSince >= 300)) {
            ticksSince = 0;
            lastSaved = saved;
            printf("[SNAP-CULL] widened verdicts so far: %u" "\n", saved);
            fflush(stdout);
        }
    }
    // The title screen's Snap Station item: the patch sets the byte when it
    // is chosen, and port 4 carries the station for the rest of this run.
    if (read_u8_mail(MailboxAddr + 0x3C) != 0) {
        write_u8(MailboxAddr + 0x3C, 0);
        station_title_reached();
    }
    if (read_u8_mail(MailboxAddr + 0x38) != 0) {
        write_u8(MailboxAddr + 0x38, 0);
        station_request_from_title();
    }
    // The Option screen's Exit Game item, chosen and confirmed: the program
    // closes the way the window's close button closes it. This runs on the
    // game thread (gtlUpdate), so the quit is posted to the main thread's
    // event loop rather than performed here.
    if (read_u8_mail(MailboxAddr + 0x3A) != 0) {
        write_u8(MailboxAddr + 0x3A, 0);
        printf("[SNAP] quit: Exit Game chosen on the Option screen\n");
        fflush(stdout);
        SDL_Event quit{};
        quit.type = SDL_QUIT;
        SDL_PushEvent(&quit);
    }
    animate_credits();

    // The SOUND bank first: the patched audio functions read its bytes
    // live, so all the host adds is persistence and its own knobs.
    const uint32_t sndSeq = read_u32_mail(MailboxAddr + 0x20);
    if (sndSeq != g_last_applied_snd_seq) {
        g_last_applied_snd_seq = sndSeq;
        int master = 0;
        bool mute = false;
        {
            std::lock_guard<std::mutex> lock(settings_mutex());
            Settings &snd = settings();
            snd.master_volume = std::min<int>(read_u8_mail(MailboxAddr + 0x28), 100);
            snd.music_volume = std::min<int>(read_u8_mail(MailboxAddr + 0x29), 100);
            snd.sfx_volume = std::min<int>(read_u8_mail(MailboxAddr + 0x2A), 100);
            snd.shutter_volume = std::min<int>(read_u8_mail(MailboxAddr + 0x2B), 100);
            snd.stereo = read_u8_mail(MailboxAddr + 0x2C) != 0;
            snd.mute_unfocused = read_u8_mail(MailboxAddr + 0x2D) != 0;
            master = snd.master_volume;
            mute = snd.mute_unfocused;
        }
        set_master_volume(master);
        set_mute_unfocused(mute);
        apply_game_settings(rdram);
        settings_mark_dirty();
    }

    // The CONTROLS bank: four mouse fields, consumed by input.cpp through
    // the settings on its next reading; the page's two game rows (Z Button,
    // Control Stick) are the game's own variables and never come here.
    const uint32_t ctlSeq = read_u32_mail(MailboxAddr + 0x60);
    if (ctlSeq != g_last_applied_ctl_seq) {
        g_last_applied_ctl_seq = ctlSeq;
        {
            std::lock_guard<std::mutex> lock(settings_mutex());
            Settings &c = settings();
            c.mouse_aim = read_u8_mail(MailboxAddr + 0x64) != 0;
            c.mouse_sensitivity = kCtlSpeeds[std::min<int>(read_u8_mail(MailboxAddr + 0x65), 10)] / 100.0f;
            c.mouse_zoom_speed = kCtlZoomShares[std::min<int>(read_u8_mail(MailboxAddr + 0x66), 3)] / 100.0f;
            c.mouse_invert_y = read_u8_mail(MailboxAddr + 0x67) != 0;
            c.gyro_aim = std::min<int>(read_u8_mail(MailboxAddr + 0x68), 2);
            c.gyro_sensitivity = kCtlSpeeds[std::min<int>(read_u8_mail(MailboxAddr + 0x69), 10)] / 100.0f;
            const int deadzone = std::min<int>(read_u8_mail(MailboxAddr + 0x6B), 8) * 5;
            if (deadzone != c.pad_deadzone) {
                c.pad_deadzone = deadzone;
                printf("[SNAP-CFG] stick dead zone: %d%%\n", deadzone);
                fflush(stdout);
            }
            const int fast = std::min<int>(read_u8_mail(MailboxAddr + 0x6C), 3) + 1;
            if (fast != c.fast_forward_speed) {
                c.fast_forward_speed = fast;
                if (fast == 1) {
                    printf("[SNAP-CFG] fast forward: off\n");
                } else {
                    printf("[SNAP-CFG] fast forward: %dx while the key is held\n", fast);
                }
                fflush(stdout);
            }
            const int slowStep = std::min<int>(read_u8_mail(MailboxAddr + 0x6D), 2);
            const int slow = (slowStep == 0) ? 1 : ((slowStep == 1) ? 2 : 4);
            if (slow != c.slow_motion_speed) {
                c.slow_motion_speed = slow;
                if (slow == 1) {
                    printf("[SNAP-CFG] slow motion: off\n");
                } else {
                    printf("[SNAP-CFG] slow motion: %dx slower while the key is held\n", slow);
                }
                fflush(stdout);
            }
            const bool swapped = read_u8_mail(MailboxAddr + 0x6A) != 0;
            if (swapped != c.pad_sticks_swapped) {
                c.pad_sticks_swapped = swapped;
                printf("[SNAP-CFG] pad sticks: %s\n",
                       swapped ? "swapped (the right stick aims, the left works the C buttons)"
                               : "normal (the left stick aims, the right works the C buttons)");
                fflush(stdout);
            }
        }
        settings_mark_dirty();
    }

    // The BUTTON SETUP page's bank: its requests and its live row values.
    poll_bind_bank();
    poll_mods_bank();
    poll_opt_bank();

    const uint32_t seq = read_u32_mail(MailboxAddr + 0x4);
    if (seq == g_last_applied_seq) {
        return;
    }
    g_last_applied_seq = seq;

    {
        std::lock_guard<std::mutex> lock(settings_mutex());
        Settings &s = settings();
        s.resolution_scale = std::min<int>(read_u8_mail(MailboxAddr + 0x8), 8);
        const uint8_t msaaIndex = read_u8_mail(MailboxAddr + 0x9);
        s.msaa = (msaaIndex >= 3) ? 8 : (msaaIndex == 2) ? 4 : (msaaIndex == 1) ? 2 : 0;
        s.widescreen = read_u8_mail(MailboxAddr + 0xA) != 0;
        {
            // Original, Display, or one of the held rates: the Manual
            // mode with that target. (Until 1.0.6 the row had two states
            // and Manual lived in F8 and the file alone.)
            const uint8_t fi = read_u8_mail(MailboxAddr + 0xB);
            if (fi <= 1) {
                s.fps_mode = fi;
            } else {
                s.fps_mode = 2;
                s.fps_manual_target = kFpsTargets[std::min<int>(fi - 2, 5)];
            }
        }
        s.upscale_2d = std::min<int>(read_u8_mail(MailboxAddr + 0xC), 2);
        s.present_filter = std::min<int>(read_u8_mail(MailboxAddr + 0xD), 2);
        s.dither_noise = read_u8_mail(MailboxAddr + 0xE) != 0;
        s.fullscreen = read_u8_mail(MailboxAddr + 0xF) != 0;
        s.downsample = std::clamp(int(read_u8_mail(MailboxAddr + 0x10)) + 1, 1, 8);
        s.three_point_filtering = read_u8_mail(MailboxAddr + 0x11) == 0;
        s.color_depth = std::min<int>(read_u8_mail(MailboxAddr + 0x12), 2);
        s.triple_buffering = read_u8_mail(MailboxAddr + 0x13) != 0;
        // The crop is consumed where F2's flip is: rt64_render_context.cpp
        // reads crop_enabled on every display list, so setting the field is
        // the whole apply. The intro byte is read by the patch straight from
        // the mailbox; the field only carries it to the file.
        s.crop_enabled = read_u8_mail(MailboxAddr + 0x14) != 0;
        s.intro_fix = read_u8_mail(MailboxAddr + 0x15) != 0;
        // Both consumed by RT64: apply_graphics_settings below always
        // queues an UpdateConfigAction (set_graphics_config never compares
        // the config), and RT64Context::update_config copies these two
        // into app_->userConfig and calls updateUserConfig, so a page edit
        // is on screen at the next display list.
        s.photo_detail = read_u8_mail(MailboxAddr + 0x16) != 0;
        s.jynx_vc = read_u8_mail(MailboxAddr + 0x17) != 0;
    }

    apply_graphics_settings();
    settings_mark_dirty();
}

// ---------------------------------------------------------------------------
// The pages from anywhere. A press of Esc or Select (input_request_menu) is
// taken here, on the game thread, once per frame's update. In a course the
// pause menu's code takes it from the mailbox and pauses the ride straight
// into the pages (patches/src/pause_menu_patch.inc). On any other screen
// there is no pause to lean on, so the press becomes an object with one
// process on it, made through the game's own omAddGObj and omCreateProcess
// with the update's own context -- exactly what the game does to start a
// screen's coroutines -- and the process is the patch's runner
// (patches/src/anywhere_patch.inc), which holds the screen still, dims it
// and runs the list. A press nobody takes (the attract demo, a course's
// intro with its input disabled) is dropped after a moment rather than
// kept until it would surprise.
// ---------------------------------------------------------------------------
extern std::atomic<bool> g_app_level_resident;   // overlay_hook.cpp: a course's code is loaded
extern std::atomic<uint32_t> g_scene_overlay_rom;   // overlay_hook.cpp: the scene overlay loaded last

// The screens the pages open on: each holds still without harm. A ride
// with its pause handler alive goes through the pause instead; a ride
// without one (the attract demo), the credits and an unknown scene are
// scripted to their music, or unknown, and the key is dropped there.
static bool scene_takes_the_pages(uint32_t rom) {
    switch (rom) {
        case 0x87A0B0u:   // camera_check
        case 0x8A70E0u:   // oaks_lab
        case 0x98C330u:   // photo_check
        case 0x9A6B10u:   // pokemon_album
        case 0x9D3230u:   // pokemon_report
        case 0x9FA580u:   // gallery
        case 0xA08E30u:   // main_menu
        case 0xA5CC50u:   // menu_new_game
            return true;
        default:
            return false;
    }
}

namespace {
constexpr uint32_t kRunnerObjectId = 0x534E4150u;   // 'SNAP': the runner's GObj id
constexpr uint32_t kOhUpdateDefault = 0x8000BC84u;  // the object's update: the game's default
constexpr uint32_t kRunnerFunction = 0x800BF444u;   // func_800BF444_5C2E4, replaced by the patch
constexpr uint32_t kGObjListHead = 0x8004A9E8u;     // omGObjListHead[32]
uint32_t g_menu_req_age = 0;

uint32_t rd32(const uint8_t* rdram, uint32_t addr) {
    uint32_t v;
    std::memcpy(&v, rdram + (addr - 0x80000000u), sizeof(v));
    return v;
}

// The runner object of this scene, if an earlier press made one: it stays
// on the scene's list, empty, until the scene goes.
uint32_t find_runner_object(const uint8_t* rdram) {
    for (int link = 0; link < 32; link++) {
        uint32_t obj = rd32(rdram, kGObjListHead + uint32_t(link) * 4u);
        int guard = 0;
        while ((obj >= 0x80000000u) && (obj < 0x80800000u) && (guard++ < 4096)) {
            if (rd32(rdram, obj) == kRunnerObjectId) {
                return obj;
            }
            obj = rd32(rdram, obj + 4u);
        }
    }
    return 0;
}
} // namespace

extern "C" void omAddGObj(uint8_t* rdram, recomp_context* ctx);
extern "C" void omCreateProcess(uint8_t* rdram, recomp_context* ctx);

bool menu_pages_open() {
    if ((g_menu_rdram == nullptr) || !g_staged) {
        return false;
    }
    return read_u8_mail(PagesOpenAddr) != 0;
}

static uint16_t read_u16_mail(uint32_t addr) {
    return *reinterpret_cast<uint16_t*>(g_menu_rdram + ((addr ^ 2u) - 0x80000000u));
}

// The scene heap's three pointers and the object limit, as they were before
// the arena took their place.
struct ArenaSwap {
    uint32_t start = 0;
    uint32_t end = 0;
    uint32_t ptr = 0;
    uint16_t maxObjects = 0;
};

static bool arena_enter(ArenaSwap& s) {
    uint32_t cursor = read_u32_mail(ArenaPtrAddr);
    if ((cursor < ArenaStart) || (cursor > ArenaEnd)) {
        cursor = ArenaStart;
    }
    if ((ArenaEnd - cursor) < 0x4000u) {
        return false;
    }
    s.start = read_u32_mail(kGeneralHeap + 4);
    s.end = read_u32_mail(kGeneralHeap + 8);
    s.ptr = read_u32_mail(kGeneralHeap + 12);
    s.maxObjects = read_u16_mail(kOmMaxObjects);
    write_u32(kGeneralHeap + 4, ArenaStart);
    write_u32(kGeneralHeap + 8, ArenaEnd);
    write_u32(kGeneralHeap + 12, cursor);
    write_u16(kOmMaxObjects, uint16_t(0xFFFFu));
    return true;
}

static void arena_leave(const ArenaSwap& s) {
    write_u32(ArenaPtrAddr, read_u32_mail(kGeneralHeap + 12));
    write_u32(kGeneralHeap + 4, s.start);
    write_u32(kGeneralHeap + 8, s.end);
    write_u32(kGeneralHeap + 12, s.ptr);
    write_u16(kOmMaxObjects, s.maxObjects);
}

// The ticks the scene has run since its overlay was loaded. A screen's
// entrance -- a dissolve, panels sliding in -- is a process like any other,
// and the freeze would hold it half-way with the pages drawn through it;
// the runner leaves a screen its first moments (anywhere_patch.inc).
static uint32_t g_scene_age = 0;

void menu_arena_reset(uint8_t* rdram) {
    if (rdram == nullptr) {
        return;
    }
    g_menu_rdram = rdram;
    // The object manager reads a grown object's `next` before it writes it
    // (sys/om.c, omGetGObj), so the arena must read as zero wherever the
    // cursor will pass again.
    uint32_t cursor = read_u32_mail(ArenaPtrAddr);
    if ((cursor > ArenaStart) && (cursor <= ArenaEnd)) {
        memset(rdram + (ArenaStart - 0x80000000u), 0, size_t(cursor - ArenaStart));
    }
    write_u32(ArenaPtrAddr, ArenaStart);
    g_scene_age = 0;
    write_u32(SceneAgeAddr, 0);
}

void menu_anywhere_tick(uint8_t* rdram, void* ctxIn) {
    if(snap::vr::requested.load()) {
        input_take_menu_request();
        return;
    }
    if ((rdram == nullptr) || (ctxIn == nullptr) || !g_staged) {
        return;
    }
    g_menu_rdram = rdram;
    if (g_scene_age < 0x7FFFFFFFu) {
        g_scene_age++;
    }
    write_u32(SceneAgeAddr, g_scene_age);
    {
        const uint8_t note = read_u8_mail(RunnerNoteAddr);
        if (note != 0) {
            write_u8(RunnerNoteAddr, 0);
            if (note == 3) {
                const uint32_t stat = read_u32_mail(ObjStatAddr);
                printf("[SNAP-MENU] the pages from here: opened on scene %06X, %u ticks old (its heap had %u bytes free, %u objects of a limit of %d, its display list %u bytes to spare)\n",
                       g_scene_overlay_rom.load(std::memory_order_relaxed), g_scene_age,
                       read_u32_mail(HeapFreeAddr), stat >> 16, int(int16_t(stat & 0xFFFFu)),
                       read_u32_mail(DlSpareAddr));
            } else {
                printf("[SNAP-MENU] the pages from here: %s\n",
                       (note == 2) ? "a page was already up"
                       : (note == 4) ? "not opened, a fade stayed over the screen"
                       : (note == 5) ? "not opened, their memory is spent on this screen" : "no staged strings");
            }
            fflush(stdout);
        }
    }
    // The pause handler's heartbeat, counted down: a ride is on while it
    // is above zero. The level's code is loaded on the title and in its
    // demo too, so residency alone would send the key to a handler that is
    // not there.
    const uint8_t alive = read_u8_mail(PauseAliveAddr);
    if (alive != 0) {
        write_u8(PauseAliveAddr, uint8_t(alive - 1));
    }
    // A request the pause code has not taken ages out.
    if (read_u8_mail(MenuReqAddr) != 0) {
        if (++g_menu_req_age > 45) {
            write_u8(MenuReqAddr, 0);
            g_menu_req_age = 0;
        }
    } else {
        g_menu_req_age = 0;
    }
    if (!input_take_menu_request()) {
        return;
    }
    if (read_u8_mail(PagesOpenAddr) != 0) {
        // A page is up: the press is Start, which closes it.
        input_tap_start();
        return;
    }
    if (alive != 0) {
        write_u8(MenuReqAddr, 1);
        g_menu_req_age = 0;
        return;
    }
    {
        const uint32_t scene = g_scene_overlay_rom.load(std::memory_order_relaxed);
        if (!scene_takes_the_pages(scene)) {
            printf("[SNAP-MENU] the pages do not open here (%s)\n",
                   (scene == 0x4F0610u) ? "a ride without its pause: the demo, or an intro"
                   : (scene == 0xA93460u) ? "the credits" : "a scene the port does not know");
            fflush(stdout);
            return;
        }
    }
    {
        const uint32_t spare = read_u32_mail(DlSpareAddr);
        if (spare < PagesDlNeed) {
            printf("[SNAP-MENU] the pages do not open here: this screen's display list had %u bytes to spare last frame, and the pages may draw %u\n",
                   spare, PagesDlNeed);
            fflush(stdout);
            return;
        }
    }
    recomp_context* ctx = static_cast<recomp_context*>(ctxIn);
    // The runner's object, thread, stack and process come from the scene's
    // pools, which grow from the heap when a free list is dry: the arena
    // takes the heap's place for these two calls, as it does for the pages.
    ArenaSwap swap;
    if (!arena_enter(swap)) {
        printf("[SNAP-MENU] the pages could not open here: their memory is spent on this screen\n");
        fflush(stdout);
        return;
    }
    uint32_t obj = find_runner_object(rdram);
    if (obj == 0) {
        recomp_context c = *ctx;
        c.r4 = gpr(kRunnerObjectId);
        c.r5 = gpr(int32_t(kOhUpdateDefault));
        c.r6 = 0;
        c.r7 = gpr(int32_t(0x80000000));
        omAddGObj(rdram, &c);
        obj = uint32_t(c.r2);
    }
    if (obj == 0) {
        arena_leave(swap);
        printf("[SNAP-MENU] the pages could not open here: the scene gave no object\n");
        fflush(stdout);
        return;
    }
    recomp_context c = *ctx;
    c.r4 = gpr(int32_t(obj));
    c.r5 = gpr(int32_t(kRunnerFunction));
    c.r6 = 0;   // a coroutine of its own
    c.r7 = 9;
    omCreateProcess(rdram, &c);
    arena_leave(swap);
}

void mods_drop_file(const char* path) {
    if (path == nullptr) {
        return;
    }
    const std::filesystem::path src = std::filesystem::u8path(path);
    std::string ext = src.extension().string();
    for (char& c : ext) {
        c = char(tolower(uint8_t(c)));
    }
    if ((ext != ".nrm") && (ext != ".zip")) {
        printf("[SNAP-MENU] dropped %s: not a mod (.nrm); ignored\n", path);
        fflush(stdout);
        return;
    }
    const std::filesystem::path dir = recomp::mods::get_mods_directory();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path dst = dir / src.filename();
    std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        printf("[SNAP-MENU] dropped %s: could not copy it into the mods folder (%s)\n", path, ec.message().c_str());
    } else {
        printf("[SNAP-MENU] dropped %s: copied into the mods folder; it loads at the next start\n", path);
    }
    fflush(stdout);
}

} // namespace snap
