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
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include "audio.h"
#include "paths.h"
#include "settings.h"
#include "version.h"

// stb_image's implementation is compiled inside the RT64 library
// (rt64_texture_cache.cpp); this include only brings the declarations.
#include "stb/stb_image.h"

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
// texels on the black backdrop). 0x400 of directory seats 126 ids.
constexpr uint32_t PixelsAddr = 0x80C01400u;
constexpr uint32_t MailboxMagic = 0x53474658u;   // 'SGFX'
constexpr uint32_t DirectoryMagic = 0x53474130u; // 'SGA0'
constexpr uint32_t STR_ITEM_LABEL_ID = 1;        // "Graphics", the Option item

// The strips are exactly as tall as the original menu sprites: ten rows.
constexpr int StripHeight = kMenuFontCellH;

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
Strip compose(const char* text) {
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
    stbi_uc* data = load_png(base_path("menu_text") / (std::string(name) + ".png"), &w, &h, &comp);
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

void seed_mailbox() {
    const Settings &s = settings();
    write_u8(MailboxAddr + 0x8, uint8_t(s.resolution_scale));
    const uint8_t msaaIndex = (s.msaa >= 8) ? 3 : (s.msaa >= 4) ? 2 : (s.msaa >= 2) ? 1 : 0;
    write_u8(MailboxAddr + 0x9, msaaIndex);
    write_u8(MailboxAddr + 0xA, s.widescreen ? 1 : 0);
    write_u8(MailboxAddr + 0xB, (s.fps_mode != 0) ? 1 : 0);
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
    write_u32(MailboxAddr + 0x4, 0);
    // The SOUND bank: its own sequence word and six value bytes, read live
    // by the patched audio functions (volumes as straight percentages) and
    // edited by the SOUND page.
    write_u8(MailboxAddr + 0x28, uint8_t(std::clamp(s.master_volume, 0, 100)));
    write_u8(MailboxAddr + 0x29, uint8_t(std::clamp(s.music_volume, 0, 100)));
    write_u8(MailboxAddr + 0x2A, uint8_t(std::clamp(s.sfx_volume, 0, 100)));
    write_u8(MailboxAddr + 0x2B, uint8_t(std::clamp(s.shutter_volume, 0, 100)));
    write_u8(MailboxAddr + 0x2C, s.stereo ? 1 : 0);
    write_u8(MailboxAddr + 0x2D, s.mute_unfocused ? 1 : 0);
    write_u32(MailboxAddr + 0x20, 0);
    write_u32(MailboxAddr + 0x0, MailboxMagic);
    g_last_applied_seq = 0;
    g_last_applied_snd_seq = 0;
}

} // namespace

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
        { "Original keeps the native frame pace.",     "Display interpolates to your monitor." },
        { "Classic keeps 2D art at its original size.","Sharp redraws it at high resolution." },
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
    // Ids BaseCount+52..+55: the Graphics page's last two rows -- Overscan
    // Crop, then Cutscene Fix -- a label and a description apiece.
    constexpr uint32_t StringCount = BaseCount + 56;

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
        stbi_uc* data = load_png(base_path("menu_text/recomp_logo.png"), &lw, &lh, &comp);
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
        if (chunks > 4) {
            // The patch's swap path carries at most four chunks, and nine
            // help lines already pad to exactly 256 -- a reworded line
            // that spills over would truncate on screen with no other
            // symptom, so the spill announces itself here instead.
            printf("[SNAP-GFX] WARNING: string %d is %dpx wide (%d chunks); "
                   "the menu draws at most 4 -- it will truncate at 256px\n",
                   id, w, chunks);
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
        s.fps_mode = (read_u8_mail(MailboxAddr + 0xB) != 0) ? 1 : 0;
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
    }

    apply_graphics_settings();
    settings_mark_dirty();
}

} // namespace snap
