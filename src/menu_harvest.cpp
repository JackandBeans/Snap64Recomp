/**
 * @file menu_harvest.cpp
 * @brief Cuts the menu sprite fonts out of RDRAM. See menu_harvest.h.
 *
 * Every routine here mirrors one in tools/harvest_menu_font.py or
 * tools/extract_menu_dot.py, and keeps that routine's quirks on purpose: the
 * tables it builds must equal, texel for texel, the ones those scripts
 * produced from the ROM, because those are the tables every layout in
 * menu_assets.cpp was tuned against.
 */
#include "menu_harvest.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "hle/rt64_snap_diag.h"

namespace snap {

namespace {

constexpr int Core = 128;   // CORE in harvest_menu_font.py: ink is alpha >= 128

struct Px {
    uint8_t i = 0;
    uint8_t a = 0;
};

struct Img {
    int w = 0;
    int h = 0;
    std::vector<Px> px;
    Px at(int x, int y) const { return px[size_t(y) * size_t(w) + size_t(x)]; }
};

// RDRAM through the recompiler's addressing (recomp.h MEM_W / MEM_H / MEM_B):
// words at addr, halves at addr ^ 2, bytes at addr ^ 3. Every access is
// checked against the segment first, so a header that points outside it fails
// the harvest instead of reading whatever else happens to be in RDRAM.
class Segment {
public:
    explicit Segment(const uint8_t* rdram) : rdram_(rdram) {}

    bool holds(uint32_t addr, uint32_t len) const {
        return (addr >= kMainMenuVpk0Vram) && (len <= kMainMenuVpk0Size) &&
               ((addr - kMainMenuVpk0Vram) <= (kMainMenuVpk0Size - len));
    }
    uint32_t u32(uint32_t addr) const {
        uint32_t v;
        std::memcpy(&v, rdram_ + (addr - 0x80000000u), sizeof(v));
        return v;
    }
    int16_t s16(uint32_t addr) const {
        int16_t v;
        std::memcpy(&v, rdram_ + ((addr ^ 2u) - 0x80000000u), sizeof(v));
        return v;
    }
    uint16_t u16(uint32_t addr) const {
        uint16_t v;
        std::memcpy(&v, rdram_ + ((addr ^ 2u) - 0x80000000u), sizeof(v));
        return v;
    }

private:
    const uint8_t* rdram_;
};

void fail(std::string& why, const char* what, uint32_t vram) {
    char msg[128];
    snprintf(msg, sizeof(msg), "%s (sprite 0x%08X)", what, vram);
    why = msg;
}

// extract_menu_dot.py decode_sprite: a Sprite's bitmaps laid side by side into
// one image of (intensity, alpha) pairs. Sixteen-bit texels whatever the
// format says (the RGBA16 copyright sprite decodes through the same path);
// odd rows undo the LoadBlockS word-pair shuffle; the cursor only moves
// right, so a sprite with several bitmap rows yields its first row only --
// exactly what the offline harvest saw. The bounds checks refuse what the
// script could not have read without raising (a header, bitmap table or
// pixel run outside the segment; a row narrower than the texels taken from
// it) plus size limits no menu sprite comes near; for every sprite the
// script accepted, the two decode the same texels.
bool decode_sprite(const Segment& seg, uint32_t vram, Img& img, std::string& why) {
    if (!seg.holds(vram, 0x38)) {   // the header fields read below end at +0x38
        fail(why, "sprite header outside the segment", vram);
        return false;
    }
    const int width = seg.s16(vram + 0x04);
    const int height = seg.s16(vram + 0x06);
    const int nbitmaps = seg.s16(vram + 0x28);
    const int bmHreal = seg.s16(vram + 0x2E);
    const uint32_t bitmapPtr = seg.u32(vram + 0x34);
    if ((width <= 0) || (width > 512) || (height <= 0) || (height > 64) ||
        (nbitmaps <= 0) || (nbitmaps > 64) || (bmHreal < 0) ||
        !seg.holds(bitmapPtr, uint32_t(nbitmaps) * 0x10u)) {
        fail(why, "implausible sprite header", vram);
        return false;
    }

    img.w = width;
    img.h = height;
    img.px.assign(size_t(width) * size_t(height), Px{});

    int xCursor = 0;
    for (int bi = 0; bi < nbitmaps; bi++) {
        const uint32_t b = bitmapPtr + uint32_t(bi) * 0x10u;
        const int bw = seg.s16(b + 0x0);
        const int bwImg = seg.s16(b + 0x2);
        const uint32_t buf = seg.u32(b + 0x8);
        int rows = seg.s16(b + 0xC);
        if (rows == 0) {
            rows = bmHreal;
        }
        if ((bw < 0) || (bwImg < 0) || (rows < 0)) {
            fail(why, "implausible bitmap", vram);
            return false;
        }
        const int rowBytes = bwImg * 2;
        const int rowsUsed = std::min(rows, height);
        const int texels = std::min(bw, width - xCursor);   // <= 0: nothing to place
        if ((rowsUsed > 0) && (texels > 0)) {
            if (texels * 2 > rowBytes) {
                fail(why, "bitmap texels past the end of its rows", vram);
                return false;
            }
            if (!seg.holds(buf, uint32_t(rowBytes) * uint32_t(rowsUsed))) {
                fail(why, "bitmap pixels outside the segment", vram);
                return false;
            }
        }
        for (int ry = 0; ry < rowsUsed; ry++) {
            for (int p = 0; p < texels; p++) {
                int o = p * 2;
                if ((ry & 1) && (o < (rowBytes & ~7))) {
                    o ^= 4;   // the shuffle swaps the two words of every whole 8-byte group
                }
                const uint16_t v = seg.u16(buf + uint32_t(ry * rowBytes + o));
                img.px[size_t(ry) * size_t(width) + size_t(xCursor + p)] = Px{ uint8_t(v >> 8), uint8_t(v & 0xFF) };
            }
        }
        xCursor += bw;
    }
    return true;
}

using Span = std::pair<int, int>;   // [start, end)

// harvest_menu_font.py find_lines: rows split into ink bands separated by
// fully blank rows.
std::vector<Span> find_lines(const Img& img) {
    std::vector<Span> bands;
    int y = 0;
    auto inked = [&img](int row) {
        for (int x = 0; x < img.w; x++) {
            if (img.at(x, row).a >= Core) {
                return true;
            }
        }
        return false;
    };
    while (y < img.h) {
        if (!inked(y)) {
            y++;
            continue;
        }
        const int y0 = y;
        while ((y < img.h) && inked(y)) {
            y++;
        }
        bands.push_back(Span{ y0, y });
    }
    return bands;
}

// harvest_menu_font.py runs_in: the ink columns of one band, as runs.
std::vector<Span> runs_in(const Img& img, int y0, int y1) {
    std::vector<bool> occ(size_t(img.w), false);
    for (int x = 0; x < img.w; x++) {
        for (int y = y0; y < y1; y++) {
            if (img.at(x, y).a >= Core) {
                occ[size_t(x)] = true;
                break;
            }
        }
    }
    std::vector<Span> runs;
    int x = 0;
    while (x < img.w) {
        if (!occ[size_t(x)]) {
            x++;
            continue;
        }
        const int s = x;
        while ((x < img.w) && occ[size_t(x)]) {
            x++;
        }
        runs.push_back(Span{ s, x });
    }
    return runs;
}

struct Cell {
    int w = 0;    // cell width
    int cs = 0;   // core start inside the cell
    int ce = 0;   // core end
    std::vector<Px> px;   // cellH rows of w
};

// The harvester's cut: one column of fringe either side of the run, cellH
// rows from the row above the band's top; rows outside the image are (0,0).
Cell cut_cell(const Img& img, int s, int e, int top, int cellH) {
    Cell c;
    const int x0 = std::max(0, s - 1);
    const int x1 = std::min(img.w, e + 1);
    c.w = x1 - x0;
    c.cs = s - x0;
    c.ce = e - x0;
    c.px.assign(size_t(c.w) * size_t(cellH), Px{});
    for (int r = 0; r < cellH; r++) {
        const int y = top - 1 + r;
        if ((y < 0) || (y >= img.h)) {
            continue;
        }
        for (int x = x0; x < x1; x++) {
            c.px[size_t(r) * size_t(c.w) + size_t(x - x0)] = img.at(x, y);
        }
    }
    return c;
}

// The port's own drawings, in the harvester's notation: '#' solid, '%'
// strong fringe, '+' soft fringe, '.' clear. Ten rows for the body and
// credits faces, twelve for the header and help faces.
struct SynthGlyph {
    char ch;
    const char* rows[12];
};

uint8_t level(char ch) {
    switch (ch) {
        case '#': return 255;
        case '%': return 160;
        case '+': return 80;
        default:  return 0;
    }
}

// synth_cell (body) and the header conversion: the full drawn width, the core
// spanning the whole cell, intensity 255 everywhere.
Cell synth_full(const SynthGlyph& g, int cellH) {
    Cell c;
    c.w = int(std::strlen(g.rows[0]));
    c.cs = 0;
    c.ce = c.w;
    c.px.assign(size_t(c.w) * size_t(cellH), Px{});
    for (int r = 0; r < cellH; r++) {
        for (int x = 0; x < c.w; x++) {
            c.px[size_t(r) * size_t(c.w) + size_t(x)] = Px{ 255, level(g.rows[r][x]) };
        }
    }
    return c;
}

// The credits and help conversions: trimmed to the columns that hold a '#'
// so the advance matches the drawn width (min/max default to 0 when there is
// none, as in the script). Credits keep only '#' as opaque; help keeps the
// fringe levels.
Cell synth_trimmed(const SynthGlyph& g, int cellH, bool binary) {
    int x0 = -1;
    int x1 = -1;
    for (int r = 0; r < cellH; r++) {
        const int w = int(std::strlen(g.rows[r]));
        for (int x = 0; x < w; x++) {
            if (g.rows[r][x] == '#') {
                x0 = (x0 < 0) ? x : std::min(x0, x);
                x1 = std::max(x1, x + 1);
            }
        }
    }
    if (x0 < 0) {
        x0 = 0;
        x1 = 1;
    }
    Cell c;
    c.w = x1 - x0;
    c.cs = 0;
    c.ce = c.w;
    c.px.assign(size_t(c.w) * size_t(cellH), Px{});
    for (int r = 0; r < cellH; r++) {
        for (int x = x0; x < x1; x++) {
            const char ch = g.rows[r][x];
            c.px[size_t(r) * size_t(c.w) + size_t(x - x0)] =
                Px{ 255, binary ? uint8_t((ch == '#') ? 255 : 0) : level(ch) };
        }
    }
    return c;
}

// --- transcripts -----------------------------------------------------------
// (vram, text): '|' separates lines within one sprite; '<', '>' and '@' (the
// bullet dot) mark runs to skip. Ordered so the Options screen's own strips
// take priority for duplicated characters. The title-menu items use a larger
// variant of this face and are deliberately absent.
struct Source {
    uint32_t vram;
    const char* text;
};

constexpr Source kBodySources[] = {
    { 0x8033F498, "@Screen" },
    { 0x8033FBB8, "@Sound" },
    { 0x8033E938, "@Z Button Setup" },
    { 0x8033E210, "@Control Stick Setup" },
    { 0x8033ECD0, "@Return" },
    { 0x80342FF0, "<Stereo>" },
    { 0x80342150, "<Mono>" },
    { 0x80341C70, "<Hold>" },
    { 0x803434D0, "<Switch>" },
    { 0x80342B10, "<Reverse>" },
    { 0x80342630, "<Normal>" },
    { 0x8032F360, "Display setting on screen." },
    { 0x80339F50, "Sound setting." },
    { 0x80336600, "Set sound to Mono." },
    { 0x8033D8A0, "Set sound to Stereo." },
    { 0x803280C0, "Z Button setting." },
    { 0x80320E20, "Hold Z Button to focus." },
    { 0x80324770, "Press Z Button to focus, and press|Z Button again to release." },
    { 0x80316230, "Control Stick setting." },
    { 0x80319B80, "Press up on Control Stick to raise view.|Press down on Control Stick to lower view." },
    { 0x8031D4D0, "Press up on Control Stick to lower view.|Press down on Control Stick to raise view." },
    { 0x8032BA10, "Return to title screen." },
    { 0x80332CB0, "Adjust orange frame by moving Control Stick." },
};

// The help face: the twelve stock help sentences (238x30 IA16, all of the
// antialiasing in alpha). The canonical box sentence goes first so its cells
// and gaps win the first-occurrence contest.
constexpr Source kHelpSources[] = {
    { 0x8032F360, "Display setting on screen." },
    { 0x80316230, "Control Stick setting." },
    { 0x80319B80, "Press up on Control Stick to raise view.|Press down on Control Stick to lower view." },
    { 0x8031D4D0, "Press up on Control Stick to lower view.|Press down on Control Stick to raise view." },
    { 0x80320E20, "Hold Z Button to focus." },
    { 0x80324770, "Press Z Button to focus, and press|Z Button again to release." },
    { 0x803280C0, "Z Button setting." },
    { 0x8032BA10, "Return to title screen." },
    { 0x80332CB0, "Adjust orange frame by moving Control Stick." },
    { 0x80336600, "Set sound to Mono." },
    { 0x80339F50, "Sound setting." },
    { 0x8033D8A0, "Set sound to Stereo." },
};

// The header face: the one sprite in the Options title's medium face.
constexpr Source kHeaderSource = { 0x80341790, "Options" };

// The credits face: the copyright block. No commas in the transcript -- their
// tails tuck under the neighbouring digits' columns and never form runs.
constexpr Source kCreditsSource = { 0x802F82C8, "@1995 1996 1998 Nintendo/Creatures/GAMEFREAK" };

// The two sprites the furniture is cut from.
constexpr uint32_t kLabelVram = 0x8033F498;   // "@Screen": the bullet dot
constexpr uint32_t kValueVram = 0x80342FF0;   // "<Stereo>": the chevrons

// --- the port's own glyphs (harvest_menu_font.py SYNTH, HLP_SYNTH, CRD_SYNTH,
// HDR_SYNTH, transcribed verbatim) ------------------------------------------

// The twenty-two body characters no menu sprite contains, drawn in the
// sprite font's own style (2px stems, one antialiased fringe, caps on rows
// 1..9). J is the port's, for the Jynx Recolour row: T's bar, a stem on the
// right, O's bottom curve opening to the left.
constexpr SynthGlyph kBodySynth[] = {
    { 'J', { ".......", "+#####.", ".+++##+", "....##.", "....#+.", "....#+.", "....#+.", "+#..#+.", ".####+.", ".+##+.." } },
    { 'E', { ".......", "+#####.", ".##+++.", ".#+....", ".####+.", ".##+++.", ".#+....", ".#+..+.", ".#####.", ".+###+." } },
    { 'V', { ".......", "##...##", "#+...#+", "#+...#+", "#+...#+", "+#..+#.", ".#+.#+.", ".##+#+.", ".+##+..", "..##..." } },
    { '0', { ".....", "+###+", "##+##", "#+.#+", "#+.#+", "#+.#+", "#+.#+", "#+.#+", "####+", "+###+" } },
    { '9', { ".....", "+###+", "##+##", "#+.#+", "#+.#+", "+####", "..+##", "..+#+", ".+##+", "+##+." } },
    { 'O', { ".......", ".......", ".+###+.", ".##+##.", "+#%.##.", "+#..+#.", "+#..+#.", ".#+.+#.", ".##+##.", ".+###+." } },
    { 'G', { ".......", ".......", ".+###+.", ".##+#%.", "+#%.%+.", "+#.....", "+#.+##.", ".#+.+#.", ".##+##.", ".%###+." } },
    { 'W', { ".......", "#+...+#", "#+...+#", "#+...+#", "#+.+.+#", "#++#++#", "#+###+#", "###+###", "##+.+##", "#+...+#" } },
    { 'T', { "......", "######", "##+###", "+.##.+", "..##..", "..#+..", "..#+..", "..#+..", "..##..", "..##.." } },
    { 'F', { ".......", "+#####.", ".##+++.", ".#+....", ".####+.", ".##+++.", ".#+....", ".#+....", ".#+....", ".##...." } },
    { 'L', { ".......", ".##....", ".#+....", ".#+....", ".#+....", ".#+....", ".#+....", ".#+..+.", ".#####.", ".+###+." } },
    { '1', { ".....", ".+#+.", "+##+.", "#+#+.", "..#+.", "..#+.", "..#+.", "..#+.", ".###+", ".+#+." } },
    { '2', { ".....", "+###+", "##+##", "+..#+", "..+#+", ".+#+.", ".##..", "+#+..", "#####", "+###+" } },
    { '3', { ".....", "+###+", "##+##", "...#+", ".+##+", ".+###", "...##", "+..#+", "#####", "+###+" } },
    { '4', { ".....", "..+#+", ".+###", ".##.#", "+#+.#", "##..#", "#####", "+++##", "...#+", "...#+" } },
    { '5', { ".....", "#####", "#+++.", "#+...", "####+", "+++##", "...#+", "+..##", "####+", "+##+." } },
    { '6', { ".....", ".+##+", ".##+#", "+#+..", "####+", "##+##", "#+.#+", "#+.#+", "####+", "+##+." } },
    { '7', { ".....", "#####", "#++##", "..+#+", "..##.", ".+#+.", ".##..", ".#+..", ".#+..", ".#+.." } },
    { '8', { ".....", "+###+", "##+##", "#+.#+", "+###+", "##+##", "#+.#+", "#+.#+", "####+", "+###+" } },
    { 'x', { ".....", ".....", ".....", ".....", "##.##", "+#+#+", ".+#+.", ".###.", "##+##", "#+.+#" } },
    { 'z', { ".....", ".....", ".....", ".....", "####+", "++##+", ".+#+.", ".##..", "#####", "+###+" } },
    { '-', { ".....", ".....", ".....", ".....", ".....", "####+", "+++#.", ".....", ".....", "....." } },
};

// Characters the twelve help sentences never use, in the help face's own
// proportions (9-row caps on rows 1-9, x-height rows 4-9).
constexpr SynthGlyph kHelpSynth[] = {
    { 'J', { ".....", ".####", "...#.", "...#.", "...#.", "...#.", "...#.", "...#.", "#..#.", "+##+.", ".....", "....." } },
    { 'L', { "....", "#...", "#...", "#...", "#...", "#...", "#...", "#...", "#..+", "####", "....", "...." } },
    { 'N', { ".....", "#...#", "##..#", "##+.#", "#.#.#", "#.#.#", "#.+##", "#..##", "#...#", "#...#", ".....", "....." } },
    { 'O', { ".+++.", "+###+", "#+.+#", "#...#", "#...#", "#...#", "#...#", "#...#", "#+.+#", "+###+", ".....", "....." } },
    { 'T', { ".....", "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#..", "..#..", "..#..", ".....", "....." } },
    { 'W', { ".....", "#...#", "#...#", "#...#", "#...#", "#.+.#", "#.#.#", "#.#.#", "#%#%#", "+#.#+", ".....", "....." } },
    { 'h', { "...", "#..", "#..", "#..", "#%#", "#.#", "#.#", "#.#", "#.#", "#.#", "...", "..." } },
    { 'x', { "...", "...", "...", "...", "#.#", "#+#", ".#.", ".#.", "#+#", "#.#", "...", "..." } },
    { 'z', { "....", "....", "....", "....", "####", "...#", "..#+", ".#+.", "#+..", "####", "....", "...." } },
    { '2', { "....", "+##+", "#..#", "...#", "...#", "..+#", ".+#+", "+#+.", "#...", "####", "....", "...." } },
    { '3', { "....", "+##+", "#..#", "...#", ".+#+", "...#", "...#", "...#", "#..#", "+##+", "....", "...." } },
    { '4', { "....", "..+#", ".+##", "+#.#", "#..#", "####", "...#", "...#", "...#", "...#", "....", "...." } },
    { '6', { "....", "+##+", "#..+", "#...", "###+", "#..#", "#..#", "#..#", "#..#", "+##+", "....", "...." } },
};

// Characters the copyright text lacks, in its 1px condensed style. '\x01' is
// the credits face's middle dot.
constexpr SynthGlyph kCreditsSynth[] = {
    { 'J', { ".....", "..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##..", ".....", "....." } },
    { 'B', { ".....", "###..", "#..#.", "#..#.", "###..", "#..#.", "#..#.", "###..", ".....", "....." } },
    { 'S', { ".....", ".###.", "#....", "#....", ".##..", "...#.", "...#.", "###..", ".....", "....." } },
    { 'k', { ".....", "#....", "#....", "#..#.", "#.#..", "##...", "#.#..", "#..#.", ".....", "....." } },
    { 'm', { ".....", ".....", ".....", "####.", "#.#.#", "#.#.#", "#.#.#", "#.#.#", ".....", "....." } },
    { 'p', { ".....", ".....", ".....", "###..", "#..#.", "#..#.", "###..", "#....", "#....", "....." } },
    { 'c', { ".....", ".....", ".....", ".###.", "#....", "#....", "#....", ".###.", ".....", "....." } },
    { 'v', { ".....", ".....", ".....", "#..#.", "#..#.", "#..#.", ".##..", ".##..", ".....", "....." } },
    { '&', { ".....", ".#...", "#.#..", "#.#..", ".#...", "#.#.#", "#..#.", ".##.#", ".....", "....." } },
    { '(', { ".....", "..#..", ".#...", ".#...", ".#...", ".#...", ".#...", "..#..", ".....", "....." } },
    { ')', { ".....", "..#..", "...#.", "...#.", "...#.", "...#.", "...#.", "..#..", ".....", "....." } },
    { '0', { ".....", ".##..", "#..#.", "#..#.", "#..#.", "#..#.", "#..#.", ".##..", ".....", "....." } },
    { '4', { ".....", "..##.", ".#.#.", "#..#.", "####.", "...#.", "...#.", "...#.", ".....", "....." } },
    { '.', { ".....", ".....", ".....", ".....", ".....", ".....", ".....", "#....", ".....", "....." } },
    { '\x01', { ".....", ".....", ".....", ".....", "##...", "##...", ".....", ".....", ".....", "....." } },
};

// The letters "Graphics" and "Sound" need beyond the "Options" sprite, in the
// header face's style: 2px strokes, squarish bowls, minimal antialiasing.
constexpr SynthGlyph kHeaderSynth[] = {
    { 'S', { "........", ".+####+.", "+######.", "##+..+#.", "##+.....", "+####+..", "..+####+", ".....+##", "#+...+##", ".######+", ".+####+.", "........" } },
    { 'u', { ".......", ".......", ".......", ".......", "#+...##", "#+...##", "#+...##", "#+...##", "##...##", "##+.+##", ".#####+", "......." } },
    { 'd', { ".......", "....+##", "....+##", "....+##", ".+##+##", "+#+.+##", "##...##", "##...##", "##..+##", "##+.+##", ".#####+", "......." } },
    { 'G', { "........", ".+####+.", ".######+", "##+..+##", "##......", "#+......", "#+..+###", "##....##", "##...+##", ".######+", ".+####+.", "........" } },
    { 'r', { ".......", ".......", ".......", ".......", "#+.###.", "#+####+", "###+.#+", "##+....", "#+.....", "#+.....", "##.....", "......." } },
    { 'a', { ".......", ".......", ".......", ".......", ".+####+", ".#++.##", "....+##", ".+#####", "##+..##", "##..+##", ".#####+", "......." } },
    { 'c', { ".......", ".......", ".......", ".......", ".+####.", ".##++#+", "##+....", "#+.....", "##.....", ".##++#.", ".+####.", "......." } },
    { 'h', { ".......", ".##....", ".#+....", ".#+....", ".#+###.", ".######", ".##+.##", ".#+..##", ".#+..##", ".#+..##", ".##..##", "......." } },
};

// --- the four faces --------------------------------------------------------

using Table = std::map<char, Cell>;   // ascending by character, like sorted()
using KernTable = std::map<std::pair<char, char>, uint8_t>;

std::vector<std::string> split_lines(const char* text) {
    std::vector<std::string> lines(1);
    for (const char* c = text; *c != 0; c++) {
        if (*c == '|') {
            lines.emplace_back();
        }
        else {
            lines.back().push_back(*c);
        }
    }
    return lines;
}

std::string without_spaces(const std::string& line) {
    std::string out;
    for (char c : line) {
        if (c != ' ') {
            out.push_back(c);
        }
    }
    return out;
}

// A sprite or line the transcript does not segment cleanly is skipped whole,
// as the script skips it (the one expected case is 80342B10 "<Reverse>", 8
// runs against 9 characters). Reported under SNAP_STATS only.
void note_skip(uint32_t vram, const char* what, size_t got, size_t want) {
    if (snapdiag::statsEnabled()) {
        printf("[SNAP-MENU] harvest: %08X %s: %zu vs %zu -- SKIP\n", vram, what, got, want);
    }
}

// The body face: every transcribed sprite in SOURCES order, first occurrence
// of a character winning.
bool harvest_body(const Segment& seg, Table& glyphs, std::string& why) {
    for (const Source& src : kBodySources) {
        Img img;
        if (!decode_sprite(seg, src.vram, img, why)) {
            return false;
        }
        const std::vector<std::string> lines = split_lines(src.text);
        const std::vector<Span> bands = find_lines(img);
        if (bands.size() != lines.size()) {
            note_skip(src.vram, "bands vs lines", bands.size(), lines.size());
            continue;
        }
        for (size_t li = 0; li < lines.size(); li++) {
            const std::string expect = without_spaces(lines[li]);
            const std::vector<Span> runs = runs_in(img, bands[li].first, bands[li].second);
            if (runs.size() != expect.size()) {
                note_skip(src.vram, "runs vs chars", runs.size(), expect.size());
                continue;
            }
            const int top = bands[li].first;
            for (size_t i = 0; i < expect.size(); i++) {
                const char c = expect[i];
                if ((c == '<') || (c == '>') || (c == '@') || (glyphs.count(c) != 0)) {
                    continue;
                }
                glyphs[c] = cut_cell(img, runs[i].first, runs[i].second, top, kMenuFontCellH);
            }
        }
    }
    for (const SynthGlyph& s : kBodySynth) {
        if (glyphs.count(s.ch) == 0) {
            glyphs[s.ch] = synth_full(s, kMenuFontCellH);
        }
    }
    return true;
}

// The header face: the "Options" sprite's first band, one run per letter.
bool harvest_header(const Segment& seg, Table& hdr, std::string& why) {
    Img img;
    if (!decode_sprite(seg, kHeaderSource.vram, img, why)) {
        return false;
    }
    const std::vector<Span> bands = find_lines(img);
    if (bands.empty()) {
        fail(why, "header sprite has no ink", kHeaderSource.vram);
        return false;
    }
    const std::vector<Span> runs = runs_in(img, bands[0].first, bands[0].second);
    const std::string text = kHeaderSource.text;
    if (runs.size() == text.size()) {
        const int top = bands[0].first;
        for (size_t i = 0; i < text.size(); i++) {
            hdr[text[i]] = cut_cell(img, runs[i].first, runs[i].second, top, kMenuHdrCellH);
        }
    }
    else {
        note_skip(kHeaderSource.vram, "header runs vs chars", runs.size(), text.size());
    }
    for (const SynthGlyph& s : kHeaderSynth) {
        if (hdr.count(s.ch) == 0) {
            hdr[s.ch] = synth_full(s, kMenuHdrCellH);
        }
    }
    return true;
}

// The credits face: the copyright block's first line. Digit cells are
// clipped below their baseline so a swallowed comma tail cannot ride along.
bool harvest_credits(const Segment& seg, Table& crd, std::string& why) {
    Img img;
    if (!decode_sprite(seg, kCreditsSource.vram, img, why)) {
        return false;
    }
    const std::vector<Span> bands = find_lines(img);
    if (bands.empty()) {
        fail(why, "copyright sprite has no ink", kCreditsSource.vram);
        return false;
    }
    const std::vector<Span> runs = runs_in(img, bands[0].first, bands[0].second);
    const std::string expect = without_spaces(kCreditsSource.text);
    if (runs.size() == expect.size()) {
        const int top = bands[0].first;
        for (size_t i = 0; i < expect.size(); i++) {
            const char c = expect[i];
            if (crd.count(c) != 0) {
                continue;
            }
            Cell cell = cut_cell(img, runs[i].first, runs[i].second, top, kMenuFontCellH);
            if ((c >= '0') && (c <= '9')) {
                for (int r = 8; r < kMenuFontCellH; r++) {   // row_i > 7
                    std::fill(cell.px.begin() + ptrdiff_t(r) * cell.w,
                              cell.px.begin() + ptrdiff_t(r + 1) * cell.w, Px{});
                }
            }
            crd[c] = cell;
        }
    }
    else {
        note_skip(kCreditsSource.vram, "credits runs vs chars", runs.size(), expect.size());
    }
    for (const SynthGlyph& s : kCreditsSynth) {
        if (crd.count(s.ch) == 0) {
            crd[s.ch] = synth_trimmed(s, kMenuFontCellH, true);
        }
    }
    return true;
}

// The help face and its kerning: every stock sentence, first occurrence
// winning for both the cells and the per-pair gaps.
bool harvest_help(const Segment& seg, Table& hlp, KernTable& kern, KernTable& spaceKern, std::string& why) {
    for (const Source& src : kHelpSources) {
        Img img;
        if (!decode_sprite(seg, src.vram, img, why)) {
            return false;
        }
        const std::vector<std::string> lines = split_lines(src.text);
        const std::vector<Span> bands = find_lines(img);
        if (bands.size() != lines.size()) {
            note_skip(src.vram, "help bands vs lines", bands.size(), lines.size());
            continue;
        }
        for (size_t li = 0; li < lines.size(); li++) {
            const std::string expect = without_spaces(lines[li]);
            const std::vector<Span> runs = runs_in(img, bands[li].first, bands[li].second);
            if (runs.size() != expect.size()) {
                note_skip(src.vram, "help runs vs chars", runs.size(), expect.size());
                continue;
            }
            const int top = bands[li].first;
            for (size_t i = 0; i < expect.size(); i++) {
                const char c = expect[i];
                if (hlp.count(c) != 0) {
                    continue;
                }
                hlp[c] = cut_cell(img, runs[i].first, runs[i].second, top, kMenuHlpCellH);
            }
            // Kerning: walk the line against its runs, one gap per pair.
            size_t ri = 0;
            char prev = 0;
            int prevEnd = 0;
            bool spaced = false;
            for (char c : lines[li]) {
                if (c == ' ') {
                    spaced = true;
                    continue;
                }
                const Span r = runs[ri];
                if (prev != 0) {
                    KernTable& table = spaced ? spaceKern : kern;
                    table.emplace(std::make_pair(prev, c), uint8_t(r.first - prevEnd));   // first wins
                }
                prev = c;
                prevEnd = r.second;
                spaced = false;
                ri++;
            }
        }
    }
    for (const SynthGlyph& s : kHelpSynth) {
        if (hlp.count(s.ch) == 0) {
            hlp[s.ch] = synth_trimmed(s, kMenuHlpCellH, false);
        }
    }
    return true;
}

// extract_menu_dot.py: the bullet dot is the run of solid columns before the
// label text; the chevrons are the first and last solid runs of the value
// strip.
MenuBitmap crop(const Img& img, int x0, int x1) {
    MenuBitmap b;
    b.w = x1 - x0;
    b.h = img.h;
    b.ia.reserve(size_t(b.w) * size_t(b.h) * 2);
    for (int y = 0; y < img.h; y++) {
        for (int x = x0; x < x1; x++) {
            b.ia.push_back(img.at(x, y).i);
            b.ia.push_back(img.at(x, y).a);
        }
    }
    return b;
}

std::vector<bool> col_core(const Img& img) {
    std::vector<bool> core(size_t(img.w), false);
    for (int x = 0; x < img.w; x++) {
        for (int y = 0; y < img.h; y++) {
            if (img.at(x, y).a >= Core) {
                core[size_t(x)] = true;
                break;
            }
        }
    }
    return core;
}

int index_of(const std::vector<bool>& v, bool want, int from) {
    for (size_t i = size_t(std::max(0, from)); i < v.size(); i++) {
        if (v[i] == want) {
            return int(i);
        }
    }
    return -1;
}

bool harvest_furniture(const Segment& seg, MenuFont& out, std::string& why) {
    Img label;
    Img value;
    if (!decode_sprite(seg, kLabelVram, label, why) || !decode_sprite(seg, kValueVram, value, why)) {
        return false;
    }

    const std::vector<bool> core = col_core(label);
    const int firstInk = index_of(core, true, 0);
    const int gapAfter = (firstInk < 0) ? -1 : index_of(core, false, firstInk);
    const int textInk = (gapAfter < 0) ? -1 : index_of(core, true, gapAfter);
    if (textInk < 0) {
        fail(why, "label has no dot-gap-text structure", kLabelVram);
        return false;
    }
    const int dotEnd = gapAfter - 1;
    out.dotTextStart = textInk - 1;
    out.dot = crop(label, 0, dotEnd + 2);

    const std::vector<bool> vcore = col_core(value);
    const int vFirst = index_of(vcore, true, 0);
    const int vGap = (vFirst < 0) ? -1 : index_of(vcore, false, vFirst);
    int rStart = -1;
    for (int x = value.w - 1; x >= 0; x--) {
        if (vcore[size_t(x)]) {
            rStart = x;
            break;
        }
    }
    if ((vGap < 0) || (rStart < 0)) {
        fail(why, "value strip has no chevron structure", kValueVram);
        return false;
    }
    while ((rStart > 0) && vcore[size_t(rStart - 1)]) {
        rStart--;
    }
    const int lEnd = vGap - 1;
    out.brkL = crop(value, 0, lEnd + 2);
    out.brkR = crop(value, std::max(0, rStart - 1), value.w);
    return true;
}

// emit_table: ascending character order, one packed blob.
void pack_face(const Table& table, int cellH, MenuFace& face) {
    face.cellH = cellH;
    face.glyphs.clear();
    face.ia.clear();
    for (const auto& entry : table) {
        const Cell& cell = entry.second;
        MenuGlyph g;
        g.ch = entry.first;
        g.cellW = uint8_t(cell.w);
        g.coreStart = uint8_t(cell.cs);
        g.coreW = uint8_t(cell.ce - cell.cs);
        g.off = uint16_t(face.ia.size() / 2);
        for (const Px& p : cell.px) {
            face.ia.push_back(p.i);
            face.ia.push_back(p.a);
        }
        face.glyphs.push_back(g);
    }
}

void pack_kern(const KernTable& table, std::vector<MenuKern>& out) {
    out.clear();
    for (const auto& entry : table) {
        out.push_back(MenuKern{ entry.first.first, entry.first.second, entry.second });
    }
}

// --- JSON dump -------------------------------------------------------------

void json_char(FILE* f, char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    if ((c == '"') || (c == '\\')) {
        fprintf(f, "\\%c", c);
    }
    else if (u < 0x20) {
        fprintf(f, "\\u%04x", u);
    }
    else {
        fputc(c, f);
    }
}

void dump_face(FILE* f, const char* name, const MenuFace& face) {
    fprintf(f, "\"%s\": {", name);
    bool first = true;
    for (const MenuGlyph& g : face.glyphs) {
        fprintf(f, "%s\"", first ? "" : ", ");
        json_char(f, g.ch);
        fprintf(f, "\": [%d, %d, %d, [", g.cellW, g.coreStart, g.coreStart + g.coreW);
        const unsigned char* ia = face.ia.data() + size_t(g.off) * 2;
        for (int y = 0; y < face.cellH; y++) {
            fprintf(f, "%s[", (y == 0) ? "" : ", ");
            for (int x = 0; x < g.cellW; x++) {
                fprintf(f, "%s[%d, %d]", (x == 0) ? "" : ", ",
                        ia[(y * g.cellW + x) * 2 + 0], ia[(y * g.cellW + x) * 2 + 1]);
            }
            fprintf(f, "]");
        }
        fprintf(f, "]]");
        first = false;
    }
    fprintf(f, "}");
}

void dump_kern(FILE* f, const char* name, const std::vector<MenuKern>& kern) {
    fprintf(f, "\"%s\": [", name);
    for (size_t i = 0; i < kern.size(); i++) {
        fprintf(f, "%s[\"", (i == 0) ? "" : ", ");
        json_char(f, kern[i].a);
        fprintf(f, "\", \"");
        json_char(f, kern[i].b);
        fprintf(f, "\", %d]", kern[i].gap);
    }
    fprintf(f, "]");
}

void dump_bitmap(FILE* f, const char* name, const MenuBitmap& b) {
    fprintf(f, "\"%s\": {\"w\": %d, \"h\": %d, \"ia\": [", name, b.w, b.h);
    for (size_t i = 0; i + 1 < b.ia.size(); i += 2) {
        fprintf(f, "%s[%d, %d]", (i == 0) ? "" : ", ", b.ia[i], b.ia[i + 1]);
    }
    fprintf(f, "]}");
}

} // namespace

bool harvest_menu_font(const uint8_t* rdram, MenuFont& out) {
    if (rdram == nullptr) {
        return false;
    }
    const Segment seg(rdram);
    MenuFont font;
    Table body, hdr, crd, hlp;
    KernTable kern, spaceKern;
    std::string why;
    if (!harvest_body(seg, body, why) ||
        !harvest_header(seg, hdr, why) ||
        !harvest_credits(seg, crd, why) ||
        !harvest_help(seg, hlp, kern, spaceKern, why) ||
        !harvest_furniture(seg, font, why)) {
        printf("[SNAP-MENU] menu font harvest failed: %s\n", why.c_str());
        return false;
    }
    pack_face(body, kMenuFontCellH, font.body);
    pack_face(hdr, kMenuHdrCellH, font.hdr);
    pack_face(crd, kMenuFontCellH, font.crd);
    pack_face(hlp, kMenuHlpCellH, font.hlp);
    pack_kern(kern, font.hlpKern);
    pack_kern(spaceKern, font.hlpSpaceKern);
    font.ready = true;
    out = std::move(font);
    printf("[SNAP-MENU] menu font harvested from RDRAM: %zu body, %zu header, %zu credits, %zu help glyphs; "
           "%zu+%zu kern pairs; dot %dx%d (text at %d), chevrons %dx%d and %dx%d\n",
           out.body.glyphs.size(), out.hdr.glyphs.size(), out.crd.glyphs.size(), out.hlp.glyphs.size(),
           out.hlpKern.size(), out.hlpSpaceKern.size(), out.dot.w, out.dot.h, out.dotTextStart,
           out.brkL.w, out.brkL.h, out.brkR.w, out.brkR.h);
    if (snapdiag::statsEnabled()) {
        const char* named = std::getenv("SNAP_MENU_FONT_DUMP");
        const char* path = ((named != nullptr) && (named[0] != 0)) ? named : "menu_font_runtime.json";
        printf("[SNAP-MENU] %s %s\n", dump_menu_font(out, path) ? "wrote" : "could not write", path);
    }
    return true;
}

bool dump_menu_font(const MenuFont& font, const char* path) {
    FILE* f = fopen(path, "wb");
    if (f == nullptr) {
        return false;
    }
    fprintf(f, "{");
    dump_face(f, "body", font.body);
    fprintf(f, ", ");
    dump_face(f, "hdr", font.hdr);
    fprintf(f, ", ");
    dump_face(f, "crd", font.crd);
    fprintf(f, ", ");
    dump_face(f, "hlp", font.hlp);
    fprintf(f, ", ");
    dump_kern(f, "hlp_kern", font.hlpKern);
    fprintf(f, ", ");
    dump_kern(f, "hlp_space_kern", font.hlpSpaceKern);
    fprintf(f, ", \"dot_text_start\": %d, ", font.dotTextStart);
    dump_bitmap(f, "dot", font.dot);
    fprintf(f, ", ");
    dump_bitmap(f, "brk_l", font.brkL);
    fprintf(f, ", ");
    dump_bitmap(f, "brk_r", font.brkR);
    fprintf(f, "}\n");
    fclose(f);
    return true;
}

} // namespace snap
