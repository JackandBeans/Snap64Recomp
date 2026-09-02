/**
 * @file menu_harvest.h
 * @brief The menu sprite fonts, read out of the game's own RDRAM at run time.
 *
 * The Options screen's text is pre-rendered sprites inside the main menu's
 * VPK0 segment (ROM 0xA0F830, decompressed by the game to VRAM 0x802B5000).
 * This module cuts those sprites into per-character cells the moment the
 * game has decompressed them (src/overlay_hook.cpp, the dmaReadVPK0 wrapper),
 * so the port composites its own strings from the game's letterforms without
 * carrying any of them in the repository. The segmentation is the one
 * tools/harvest_menu_font.py and tools/extract_menu_dot.py perform offline,
 * ported line for line; the characters the sprites never contain are the
 * port's own drawings and live in menu_harvest.cpp as code.
 *
 * The segment is only resident between a main-menu load and the next thing
 * loaded over that VRAM (the intro's segment and every course's code share
 * it), so the harvest copies everything it needs into host memory and never
 * reads the segment again. It reads RDRAM and writes nothing to it.
 */
#ifndef SNAP_MENU_HARVEST_H
#define SNAP_MENU_HARVEST_H

#include <cstdint>
#include <vector>

namespace snap {

// Cell heights and gaps: the port's own measurements of the stock layouts.
constexpr int kMenuFontCellH = 10;
constexpr int kMenuHdrCellH = 12;
constexpr int kMenuHlpCellH = 12;
constexpr int kMenuFontLetterGap = 2;
constexpr int kMenuFontSpaceGap = 4;
// Measured across all 351 gaps of the twelve help sentences: intra-letter
// gaps run 1-3 with mode 2, word spaces 4-7 with mode 6.
constexpr int kMenuHlpLetterGap = 2;
constexpr int kMenuHlpSpaceGap = 6;

// The main menu's VPK0 segment: ROM start, VRAM it decompresses to, and its
// decompressed size (splat.yaml main_menu_vpk0_bss: bss_size 0xF3770).
constexpr uint32_t kMainMenuVpk0Rom  = 0xA0F830u;
constexpr uint32_t kMainMenuVpk0Vram = 0x802B5000u;
constexpr uint32_t kMainMenuVpk0Size = 0xF3770u;

struct MenuGlyph { char ch; unsigned char cellW, coreStart, coreW; unsigned short off; };
struct MenuKern { char a, b; unsigned char gap; };

// One face: glyph index plus one IA blob. Glyph g's cell is cellH rows of
// cellW IA pairs starting at ia[g.off * 2].
struct MenuFace {
    int cellH = 0;
    std::vector<MenuGlyph> glyphs;      // ascending by ch
    std::vector<unsigned char> ia;
    const MenuGlyph* find(char c) const {
        for (const MenuGlyph& g : glyphs) {
            if (g.ch == c) {
                return &g;
            }
        }
        return nullptr;
    }
};

// A piece of furniture: row-major IA pairs.
struct MenuBitmap {
    int w = 0;
    int h = 0;
    std::vector<unsigned char> ia;
};

struct MenuFont {
    bool ready = false;
    MenuFace body;      // the label/value face, kMenuFontCellH rows
    MenuFace hdr;       // the Options title's face, kMenuHdrCellH rows
    MenuFace crd;       // the copyright block's face, kMenuFontCellH rows
    MenuFace hlp;       // the help sentences' face, kMenuHlpCellH rows
    std::vector<MenuKern> hlpKern;       // (prev, next) -> gap inside a word
    std::vector<MenuKern> hlpSpaceKern;  // (prev, next) -> gap across a space
    MenuBitmap dot;     // the items' bullet dot
    MenuBitmap brkL;    // the values' left chevron
    MenuBitmap brkR;    // the values' right chevron
    int dotTextStart = 0;   // column the label text starts at, after the dot
};

// Cuts every face and the furniture out of the main menu's segment as it sits
// in RDRAM. Returns false, leaving `out` untouched, when any sprite header or
// pixel pointer falls outside the segment or the segmentation disagrees with
// the transcripts -- the caller then stages nothing and the game's own
// screens run as shipped. Under SNAP_STATS a successful harvest also writes
// the tables as JSON (dump_menu_font) to the path named by the environment
// variable SNAP_MENU_FONT_DUMP, or to menu_font_runtime.json in the working
// directory when that is unset, for the one-time comparison against the
// offline harvest.
bool harvest_menu_font(const uint8_t* rdram, MenuFont& out);

// The tables as JSON, in the shape tools/harvest_menu_font.py writes.
bool dump_menu_font(const MenuFont& font, const char* path);

} // namespace snap

#endif
