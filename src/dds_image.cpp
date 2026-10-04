// Decodes the first image of a DDS file to 8-bit RGBA, for a mod's picture
// (thumb.dds, which the runtime reads before thumb.png): uncompressed RGBA and
// BGRA, and the block formats BC1, BC2, BC3 and BC7. BC7's partition and
// anchor tables were read off a reference decoder (Pillow's) with blocks built
// to show them, and the whole decoder matches that reference on random blocks
// of every mode.
#include "dds_image.h"

#include <algorithm>
#include <cstring>

#include "ddspp/ddspp.h"

namespace {
// BC7's partition and anchor tables (read off Pillow's decoder by
// scratchpad bc7_tables.py): subset per texel, row-major.
static const uint8_t kBc7Part2[64][16] = {
    { 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1 },
    { 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1 },
    { 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1 },
    { 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 1 },
    { 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1 },
    { 0, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1 },
    { 0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1 },
    { 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1 },
    { 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
    { 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1 },
    { 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1 },
    { 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1 },
    { 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 1 },
    { 0, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0 },
    { 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0 },
    { 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0 },
    { 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1 },
    { 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0 },
    { 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0 },
    { 0, 0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 0, 0 },
    { 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0 },
    { 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 },
    { 0, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0 },
    { 0, 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0 },
    { 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1 },
    { 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1 },
    { 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0 },
    { 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0 },
    { 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0 },
    { 0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0 },
    { 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1 },
    { 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1 },
    { 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0 },
    { 0, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0 },
    { 0, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 0, 0 },
    { 0, 0, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 0 },
    { 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0 },
    { 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1 },
    { 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1 },
    { 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0 },
    { 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0 },
    { 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0 },
    { 0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1 },
    { 0, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1 },
    { 0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0 },
    { 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 0 },
    { 0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 1 },
    { 0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0, 1 },
    { 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1 },
    { 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1 },
    { 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1 },
    { 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 },
    { 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0 },
    { 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1 },
};
static const uint8_t kBc7Part3[64][16] = {
    { 0, 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 1, 2, 2, 2, 2 },
    { 0, 0, 0, 1, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 2, 1 },
    { 0, 0, 0, 0, 2, 0, 0, 1, 2, 2, 1, 1, 2, 2, 1, 1 },
    { 0, 2, 2, 2, 0, 0, 2, 2, 0, 0, 1, 1, 0, 1, 1, 1 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2 },
    { 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0, 2, 2 },
    { 0, 0, 2, 2, 0, 0, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1 },
    { 0, 0, 1, 1, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2 },
    { 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2 },
    { 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2 },
    { 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2 },
    { 0, 1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2 },
    { 0, 1, 2, 2, 0, 1, 2, 2, 0, 1, 2, 2, 0, 1, 2, 2 },
    { 0, 0, 1, 1, 0, 1, 1, 2, 1, 1, 2, 2, 1, 2, 2, 2 },
    { 0, 0, 1, 1, 2, 0, 0, 1, 2, 2, 0, 0, 2, 2, 2, 0 },
    { 0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 2, 1, 1, 2, 2 },
    { 0, 1, 1, 1, 0, 0, 1, 1, 2, 0, 0, 1, 2, 2, 0, 0 },
    { 0, 0, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2 },
    { 0, 0, 2, 2, 0, 0, 2, 2, 0, 0, 2, 2, 1, 1, 1, 1 },
    { 0, 1, 1, 1, 0, 1, 1, 1, 0, 2, 2, 2, 0, 2, 2, 2 },
    { 0, 0, 0, 1, 0, 0, 0, 1, 2, 2, 2, 1, 2, 2, 2, 1 },
    { 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 2, 2, 0, 1, 2, 2 },
    { 0, 0, 0, 0, 1, 1, 0, 0, 2, 2, 1, 0, 2, 2, 1, 0 },
    { 0, 1, 2, 2, 0, 1, 2, 2, 0, 0, 1, 1, 0, 0, 0, 0 },
    { 0, 0, 1, 2, 0, 0, 1, 2, 1, 1, 2, 2, 2, 2, 2, 2 },
    { 0, 1, 1, 0, 1, 2, 2, 1, 1, 2, 2, 1, 0, 1, 1, 0 },
    { 0, 0, 0, 0, 0, 1, 1, 0, 1, 2, 2, 1, 1, 2, 2, 1 },
    { 0, 0, 2, 2, 1, 1, 0, 2, 1, 1, 0, 2, 0, 0, 2, 2 },
    { 0, 1, 1, 0, 0, 1, 1, 0, 2, 0, 0, 2, 2, 2, 2, 2 },
    { 0, 0, 1, 1, 0, 1, 2, 2, 0, 1, 2, 2, 0, 0, 1, 1 },
    { 0, 0, 0, 0, 2, 0, 0, 0, 2, 2, 1, 1, 2, 2, 2, 1 },
    { 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 2, 2, 2 },
    { 0, 2, 2, 2, 0, 0, 2, 2, 0, 0, 1, 2, 0, 0, 1, 1 },
    { 0, 0, 1, 1, 0, 0, 1, 2, 0, 0, 2, 2, 0, 2, 2, 2 },
    { 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0 },
    { 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0 },
    { 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0 },
    { 0, 1, 2, 0, 2, 0, 1, 2, 1, 2, 0, 1, 0, 1, 2, 0 },
    { 0, 0, 1, 1, 2, 2, 0, 0, 1, 1, 2, 2, 0, 0, 1, 1 },
    { 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0, 1, 1 },
    { 0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 2, 1, 2, 1, 2, 1 },
    { 0, 0, 2, 2, 1, 1, 2, 2, 0, 0, 2, 2, 1, 1, 2, 2 },
    { 0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0, 1, 1 },
    { 0, 2, 2, 0, 1, 2, 2, 1, 0, 2, 2, 0, 1, 2, 2, 1 },
    { 0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 0, 1, 0, 1 },
    { 0, 0, 0, 0, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1 },
    { 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2 },
    { 0, 2, 2, 2, 0, 1, 1, 1, 0, 2, 2, 2, 0, 1, 1, 1 },
    { 0, 0, 0, 2, 1, 1, 1, 2, 0, 0, 0, 2, 1, 1, 1, 2 },
    { 0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2 },
    { 0, 2, 2, 2, 0, 1, 1, 1, 0, 1, 1, 1, 0, 2, 2, 2 },
    { 0, 0, 0, 2, 1, 1, 1, 2, 1, 1, 1, 2, 0, 0, 0, 2 },
    { 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 2, 2 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 1, 2 },
    { 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 2, 2, 2, 2, 2, 2 },
    { 0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 2, 2 },
    { 0, 0, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2, 0, 0, 2, 2 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2 },
    { 0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 1 },
    { 0, 2, 2, 2, 1, 2, 2, 2, 0, 2, 2, 2, 1, 2, 2, 2 },
    { 0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 },
    { 0, 1, 1, 1, 2, 0, 1, 1, 2, 2, 0, 1, 2, 2, 2, 0 },
};
static const uint8_t kBc7Anchor2[64] = { 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 2, 8, 2, 2, 8, 8, 15, 2, 8, 2, 2, 8, 8, 2, 2, 15, 15, 6, 8, 2, 8, 15, 15, 2, 8, 2, 2, 2, 15, 15, 6, 6, 2, 6, 8, 15, 15, 2, 2, 15, 15, 15, 15, 15, 2, 2, 15 };
static const uint8_t kBc7Anchor3a[64] = { 3, 3, 15, 15, 8, 3, 15, 15, 8, 8, 6, 6, 6, 5, 3, 3, 3, 3, 8, 15, 3, 3, 6, 10, 5, 8, 8, 6, 8, 5, 15, 15, 8, 15, 3, 5, 6, 10, 8, 15, 15, 3, 15, 5, 15, 15, 15, 15, 3, 15, 5, 5, 5, 8, 5, 10, 5, 10, 8, 13, 15, 12, 3, 3 };
static const uint8_t kBc7Anchor3b[64] = { 15, 8, 8, 3, 15, 15, 3, 8, 15, 15, 15, 15, 15, 15, 15, 8, 15, 8, 15, 3, 15, 8, 15, 8, 3, 15, 6, 10, 15, 15, 10, 8, 15, 3, 15, 10, 10, 8, 9, 10, 6, 15, 8, 15, 3, 6, 6, 8, 15, 3, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 3, 15, 15, 8 };

// RGB565 to 8 bits a channel, each channel's high bits repeated low.
void rgb565(uint16_t c, uint8_t out[4]) {
    const unsigned r = (c >> 11) & 31;
    const unsigned g = (c >> 5) & 63;
    const unsigned b = c & 31;
    out[0] = uint8_t((r << 3) | (r >> 2));
    out[1] = uint8_t((g << 2) | (g >> 4));
    out[2] = uint8_t((b << 3) | (b >> 2));
    out[3] = 255;
}

// A BC1 color block (8 bytes) into 16 texels. `fourColor` is BC2 and
// BC3's rule: always four colors, never BC1's transparent black.
void bc1_block(const uint8_t* b, uint8_t out[16][4], bool fourColor) {
    const uint16_t c0 = uint16_t(b[0] | (b[1] << 8));
    const uint16_t c1 = uint16_t(b[2] | (b[3] << 8));
    uint8_t p[4][4];
    rgb565(c0, p[0]);
    rgb565(c1, p[1]);
    for (int ch = 0; ch < 3; ch++) {
        if ((c0 > c1) || fourColor) {
            p[2][ch] = uint8_t((2 * p[0][ch] + p[1][ch]) / 3);
            p[3][ch] = uint8_t((p[0][ch] + 2 * p[1][ch]) / 3);
        } else {
            p[2][ch] = uint8_t((p[0][ch] + p[1][ch]) / 2);
            p[3][ch] = 0;
        }
    }
    p[2][3] = 255;
    p[3][3] = ((c0 > c1) || fourColor) ? 255 : 0;
    const uint32_t idx = uint32_t(b[4]) | (uint32_t(b[5]) << 8) | (uint32_t(b[6]) << 16) | (uint32_t(b[7]) << 24);
    for (int i = 0; i < 16; i++) {
        std::memcpy(out[i], p[(idx >> (2 * i)) & 3], 4);
    }
}

// BC2: four bits of alpha a texel, then a BC1 color block.
void bc2_block(const uint8_t* b, uint8_t out[16][4]) {
    bc1_block(b + 8, out, true);
    for (int i = 0; i < 16; i++) {
        const unsigned a = (b[i / 2] >> ((i & 1) * 4)) & 15;
        out[i][3] = uint8_t(a * 17);
    }
}

// BC3: an interpolated alpha block, then a BC1 color block.
void bc3_block(const uint8_t* b, uint8_t out[16][4]) {
    bc1_block(b + 8, out, true);
    unsigned a[8];
    a[0] = b[0];
    a[1] = b[1];
    if (a[0] > a[1]) {
        for (int i = 1; i < 7; i++) {
            a[i + 1] = ((7 - i) * a[0] + i * a[1]) / 7;
        }
    } else {
        for (int i = 1; i < 5; i++) {
            a[i + 1] = ((5 - i) * a[0] + i * a[1]) / 5;
        }
        a[6] = 0;
        a[7] = 255;
    }
    uint64_t idx = 0;
    for (int k = 0; k < 6; k++) {
        idx |= uint64_t(b[2 + k]) << (8 * k);
    }
    for (int i = 0; i < 16; i++) {
        out[i][3] = uint8_t(a[(idx >> (3 * i)) & 7]);
    }
}

struct BitReader {
    const uint8_t* b;
    int pos;
    unsigned get(int n) {
        unsigned v = 0;
        for (int i = 0; i < n; i++, pos++) {
            v |= unsigned((b[pos >> 3] >> (pos & 7)) & 1) << i;
        }
        return v;
    }
};

// BC7: eight modes, told by the lowest set bit of the first byte.
void bc7_block(const uint8_t* b, uint8_t out[16][4]) {
    struct Mode { int ns, pb, rb, isb, cb, ab, epb, spb, ib, ib2; };
    static const Mode kModes[8] = {
        { 3, 4, 0, 0, 4, 0, 1, 0, 3, 0 },
        { 2, 6, 0, 0, 6, 0, 0, 1, 3, 0 },
        { 3, 6, 0, 0, 5, 0, 0, 0, 2, 0 },
        { 2, 6, 0, 0, 7, 0, 1, 0, 2, 0 },
        { 1, 0, 2, 1, 5, 6, 0, 0, 2, 3 },
        { 1, 0, 2, 0, 7, 8, 0, 0, 2, 2 },
        { 1, 0, 0, 0, 7, 7, 1, 0, 4, 0 },
        { 2, 6, 0, 0, 5, 5, 1, 0, 2, 0 },
    };
    static const int kW2[4] = { 0, 21, 43, 64 };
    static const int kW3[8] = { 0, 9, 18, 27, 37, 46, 55, 64 };
    static const int kW4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };
    auto weight = [](int bits, unsigned i) { return (bits == 2) ? kW2[i] : (bits == 3) ? kW3[i] : kW4[i]; };

    int mode = 0;
    while ((mode < 8) && !(b[0] & (1 << mode))) {
        mode++;
    }
    if (mode == 8) {
        std::memset(out, 0, 64);   // reserved: transparent black, as the format says
        return;
    }
    const Mode& m = kModes[mode];
    BitReader r{ b, mode + 1 };
    const unsigned part = r.get(m.pb);
    const unsigned rot = r.get(m.rb);
    const unsigned isb = r.get(m.isb);
    const int ne = 2 * m.ns;
    unsigned ep[6][4] = {};
    for (int ch = 0; ch < 3; ch++) {
        for (int e = 0; e < ne; e++) {
            ep[e][ch] = r.get(m.cb);
        }
    }
    if (m.ab > 0) {
        for (int e = 0; e < ne; e++) {
            ep[e][3] = r.get(m.ab);
        }
    }
    unsigned pbit[6] = {};
    if (m.epb) {
        for (int e = 0; e < ne; e++) {
            pbit[e] = r.get(1);
        }
    }
    if (m.spb) {
        for (int s = 0; s < m.ns; s++) {
            pbit[2 * s] = pbit[2 * s + 1] = r.get(1);
        }
    }
    for (int e = 0; e < ne; e++) {
        for (int ch = 0; ch < 4; ch++) {
            if ((ch == 3) && (m.ab == 0)) {
                ep[e][3] = 255;
                continue;
            }
            int bits = (ch < 3) ? m.cb : m.ab;
            unsigned v = ep[e][ch];
            if (m.epb || m.spb) {
                v = (v << 1) | pbit[e];
                bits++;
            }
            v <<= (8 - bits);
            ep[e][ch] = v | (v >> bits);
        }
    }
    unsigned idx1[16];
    unsigned idx2[16] = {};
    int subset[16];
    for (int i = 0; i < 16; i++) {
        subset[i] = (m.ns == 1) ? 0 : (m.ns == 2) ? kBc7Part2[part][i] : kBc7Part3[part][i];
        const bool anchor = (i == 0) || ((m.ns == 2) && (i == kBc7Anchor2[part])) ||
                            ((m.ns == 3) && ((i == kBc7Anchor3a[part]) || (i == kBc7Anchor3b[part])));
        idx1[i] = r.get(m.ib - (anchor ? 1 : 0));
    }
    if (m.ib2 > 0) {
        for (int i = 0; i < 16; i++) {
            idx2[i] = r.get(m.ib2 - ((i == 0) ? 1 : 0));
        }
    }
    for (int i = 0; i < 16; i++) {
        const unsigned* e0 = ep[2 * subset[i]];
        const unsigned* e1 = ep[2 * subset[i] + 1];
        int cw;
        int aw;
        if (m.ib2 == 0) {
            cw = aw = weight(m.ib, idx1[i]);
        } else if (isb == 0) {
            cw = weight(m.ib, idx1[i]);
            aw = weight(m.ib2, idx2[i]);
        } else {
            cw = weight(m.ib2, idx2[i]);
            aw = weight(m.ib, idx1[i]);
        }
        for (int ch = 0; ch < 4; ch++) {
            const int w = (ch < 3) ? cw : aw;
            out[i][ch] = uint8_t((e0[ch] * unsigned(64 - w) + e1[ch] * unsigned(w) + 32) >> 6);
        }
        if (rot != 0) {
            std::swap(out[i][3], out[i][rot - 1]);
        }
    }
}

}  // namespace

bool dds_decode_rgba(const uint8_t* data, size_t size, int& width, int& height, std::vector<uint8_t>& rgba,
                     std::string& why) {
    unsigned char head[ddspp::MAX_HEADER_SIZE] = {};
    std::memcpy(head, data, std::min(size, sizeof(head)));
    ddspp::Descriptor desc{};
    if ((size < 4 + 124) || (ddspp::decode_header(head, desc) != ddspp::Success)) {
        why = "not a DDS file";
        return false;
    }
    using F = ddspp::DXGIFormat;
    const F f = desc.format;
    int blockBytes = 0;
    void (*decode)(const uint8_t*, uint8_t[16][4]) = nullptr;
    switch (f) {
        case F::BC1_UNORM:
        case F::BC1_UNORM_SRGB:
            blockBytes = 8;
            decode = [](const uint8_t* b, uint8_t out[16][4]) { bc1_block(b, out, false); };
            break;
        case F::BC2_UNORM:
        case F::BC2_UNORM_SRGB:
            blockBytes = 16;
            decode = bc2_block;
            break;
        case F::BC3_UNORM:
        case F::BC3_UNORM_SRGB:
            blockBytes = 16;
            decode = bc3_block;
            break;
        case F::BC7_UNORM:
        case F::BC7_UNORM_SRGB:
            blockBytes = 16;
            decode = bc7_block;
            break;
        case F::R8G8B8A8_UNORM:
        case F::R8G8B8A8_UNORM_SRGB:
        case F::B8G8R8A8_UNORM:
        case F::B8G8R8A8_UNORM_SRGB:
        case F::B8G8R8X8_UNORM:
        case F::B8G8R8X8_UNORM_SRGB:
            break;
        default:
            why = "a DDS in a format the port does not read (DXGI " + std::to_string(unsigned(f)) + ")";
            return false;
    }
    const int w = int(desc.width);
    const int h = int(desc.height);
    if ((w <= 0) || (h <= 0) || (w > 4096) || (h > 4096)) {
        why = "a DDS of an unusable size";
        return false;
    }
    const size_t at = desc.headerSize;
    rgba.assign(size_t(w) * size_t(h) * 4, 0);
    if (decode != nullptr) {
        const int bw = (w + 3) / 4;
        const int bh = (h + 3) / 4;
        if (at + size_t(bw) * size_t(bh) * size_t(blockBytes) > size) {
            why = "a DDS shorter than its image";
            return false;
        }
        uint8_t texels[16][4];
        for (int by = 0; by < bh; by++) {
            for (int bx = 0; bx < bw; bx++) {
                decode(data + at + (size_t(by) * size_t(bw) + size_t(bx)) * size_t(blockBytes), texels);
                for (int i = 0; i < 16; i++) {
                    const int x = bx * 4 + (i & 3);
                    const int y = by * 4 + (i >> 2);
                    if ((x < w) && (y < h)) {
                        std::memcpy(&rgba[(size_t(y) * size_t(w) + size_t(x)) * 4], texels[i], 4);
                    }
                }
            }
        }
    } else {
        const bool bgr = (f != F::R8G8B8A8_UNORM) && (f != F::R8G8B8A8_UNORM_SRGB);
        const bool opaque = (f == F::B8G8R8X8_UNORM) || (f == F::B8G8R8X8_UNORM_SRGB);
        const size_t pitch = std::max<size_t>(desc.rowPitch, size_t(w) * 4);
        if (at + pitch * size_t(h - 1) + size_t(w) * 4 > size) {
            why = "a DDS shorter than its image";
            return false;
        }
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                const uint8_t* s = data + at + pitch * size_t(y) + size_t(x) * 4;
                uint8_t* d = &rgba[(size_t(y) * size_t(w) + size_t(x)) * 4];
                d[0] = bgr ? s[2] : s[0];
                d[1] = s[1];
                d[2] = bgr ? s[0] : s[2];
                d[3] = opaque ? 255 : s[3];
            }
        }
    }
    width = w;
    height = h;
    return true;
}
