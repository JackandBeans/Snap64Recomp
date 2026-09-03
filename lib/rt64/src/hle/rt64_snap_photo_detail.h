//
// Pokemon Snap port: the opt-in Photo Detail path (userConfig.snapPhotoDetail).
//
// What the game does with a photo it shows. Every photo on screen -- the
// review window after a shot, Oak's check, the album, the report, the picks
// after a course -- goes through the window library's func_80374714_847EC4
// (decomp src/window/847B60.c; the function is still assembly there). It
// renders the photo through renInitCameraEx into the library's 320x210
// RGBA16 buffer at TWICE the sprite's size, then, on the CPU, halves it: each
// sprite pixel is the average of a 2x2 block of the render, taken per channel
// on the 5-bit fields of the RGBA5551 words (the four masked fields summed,
// shifted right by two, masked again), with the low bit set. The result is
// written into the sprite's own bitmap buffers -- one bitmap per strip of
// rows, odd rows stored with their 32-bit halves swapped (address xor 4), the
// TMEM layout a load with dxt zero wants -- and osWritebackDCache flushes it.
// The sprite library then draws those bitmaps with texture rectangles, and
// keeps drawing them for as long as the screen shows the photo; the buffer
// itself is rendered into again for the next photo, and for the scoring
// passes that share it.
//
// Why the renderer could not see this. RT64 serves a texture from its own
// high-resolution render only when the texture load reads an address inside a
// framebuffer it rendered (RDP::checkFramebufferOverlap, the tile copy path).
// The photo sprites never read the render buffer: they read the bitmaps the
// CPU wrote, which are ordinary heap memory. So the sprite is drawn from the
// halved RDRAM texels, and no amount of 2D upscaling can add what the console
// threw away. Measured on the eval replay (SNAP_STATS probes, Sep 2 2026): the
// photo buffer pairs render every frame a photo is shown, and not one texture
// load in the whole run touched them; the loads that do sample framebuffers
// there are the game's own scoring passes, which already take tile copies.
//
// What this does. When the setting is on, every small colour render that is
// not the screen (nothing the VI has shown) is treated as a photo the CPU may
// halve: as the pair is submitted, a tile copy of its whole extent is queued
// to run right after the pair renders -- box-filtered 2x2, the game's own
// halving applied to the port's render -- and pinned so the renderer keeps
// it while the photo may still be on screen. When the render's rows are
// written back to RDRAM (the very data the CPU halves), the bitmap the
// game's arithmetic yields from them is computed and kept beside the pin.
// A texture load from plain memory whose bytes equal that bitmap, or a run
// of its rows, is then served from the pinned copy, offset to those rows.
// Content is matched, not guessed: the whole loaded range has to equal the
// bitmap the game computed, halfword for halfword. A bounded ring of
// candidates holds the pins; a render nothing ever matched is the first to
// go when the ring is full, so the scoring passes that share the buffer
// never push a displayed photo out.
//
// Off, none of this runs: no pin, no note, no comparison.
//

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>

#include "gbi/rt64_f3d.h"
#include "hle/rt64_framebuffer.h"
#include "hle/rt64_framebuffer_manager.h"
#include "hle/rt64_snap_diag.h"

namespace RT64 {
    struct SnapPhotoDetail {
        // One render the CPU may halve, its pinned copy, and what the halving yields.
        struct Candidate {
            uint32_t address = 0;
            uint32_t width = 0;
            uint32_t dstWidth = 0;
            uint32_t dstHeight = 0;
            uint64_t tileId = 0;
            uint32_t pairIndex = 0;
            bool filled = false;
            bool duplicate = false;
            uint64_t matchedTimestamp = 0;
            bool matchReported = false;
            // The bitmap in the order the game stores it: pixel (x, y) at
            // halfword index (((y * dstWidth + x) * 2) ^ ((y & 1) ? 4 : 0)) / 2.
            std::vector<uint16_t> pixels;
        };

        struct Match {
            Candidate *candidate = nullptr;
            uint32_t row = 0;
            uint32_t rows = 0;
        };

        // The window library's buffer is 320x210; the screen is 240 rows.
        static constexpr uint32_t MaxSourceWidth = 320;
        static constexpr uint32_t MaxSourceHeight = 210;
        // Enough for every photo a screen shows at once, the previews the
        // Report renders afresh on every cursor move, and the scoring passes
        // between them; each pin is the halved photo at the render scale,
        // half a megabyte for a thumbnail at 8x.
        static constexpr size_t MaxCandidates = 40;
        static constexpr uint32_t RDRAMBytes = 0x800000;
        // A VI origin sits a row or two into its buffer; treat anything within
        // a few rows of a displayed address as the screen.
        static constexpr uint32_t ScreenTolerance = 4096;

        std::deque<Candidate> candidates;

        // RDRAM as the recompiler lays it out: a big-endian halfword at
        // address a lives at host offset a ^ 2 (rt64_rdp.cpp reads bytes at
        // address ^ 3 for the same reason).
        static uint16_t readHalf(const uint8_t *RDRAM, uint32_t address) {
            uint16_t value;
            memcpy(&value, RDRAM + (address ^ 2u), sizeof(value));
            return value;
        }

        // The game's arithmetic, per channel on the packed fields.
        static uint16_t halve(uint16_t a, uint16_t b, uint16_t c, uint16_t d) {
            const uint32_t r = (((a & 0xF800u) + (b & 0xF800u) + (c & 0xF800u) + (d & 0xF800u)) >> 2) & 0xF800u;
            const uint32_t g = (((a & 0x07C0u) + (b & 0x07C0u) + (c & 0x07C0u) + (d & 0x07C0u)) >> 2) & 0x07C0u;
            const uint32_t bl = (((a & 0x003Eu) + (b & 0x003Eu) + (c & 0x003Eu) + (d & 0x003Eu)) >> 2) & 0x003Eu;
            return uint16_t(r | g | bl | 1u);
        }

        static void unpin(FramebufferManager &fbManager, uint64_t tileId) {
            auto it = fbManager.tileCopies.find(tileId);
            if (it != fbManager.tileCopies.end()) {
                it->second.snapPinned = false;
            }
        }

        void clear(FramebufferManager &fbManager) {
            for (const Candidate &candidate : candidates) {
                unpin(fbManager, candidate.tileId);
            }

            candidates.clear();
        }

        // Whether a colour pair is one a photo could be halved from. Anything
        // the VI has displayed is the screen, whatever its size.
        static bool photoSized(uint8_t siz, uint32_t width, uint32_t drawnWidth, uint32_t drawnHeight, uint32_t address, const uint32_t *screens, size_t screenCount) {
            const bool sized = (siz == G_IM_SIZ_16b) && (width >= 2) && (width <= MaxSourceWidth) &&
                (drawnWidth >= 2) && (drawnWidth <= width) && (drawnHeight >= 2) && (drawnHeight <= MaxSourceHeight);
            if (!sized) {
                return false;
            }

            for (size_t i = 0; i < screenCount; i++) {
                const uint32_t screen = screens[i];
                if ((address + ScreenTolerance >= screen) && (screen + ScreenTolerance >= address)) {
                    return false;
                }
            }

            return true;
        }

        // Called as a colour pair is submitted, before anything renders. Pins
        // a halved copy of the whole drawn extent, queued to run right after
        // the pair, and opens a candidate the write-back fills in.
        void beginRender(FramebufferManager &fbManager, std::vector<FramebufferOperation> &endOps, uint32_t address, uint32_t width, uint8_t siz,
            uint32_t drawnWidth, uint32_t drawnHeight, uint32_t pairIndex, const uint32_t *screens, size_t screenCount)
        {
            if (!photoSized(siz, width, drawnWidth, drawnHeight, address, screens, screenCount)) {
                return;
            }

            const uint32_t dstWidth = drawnWidth >> 1;
            const uint32_t dstHeight = drawnHeight >> 1;
            const uint64_t lastByte = uint64_t(address) + uint64_t(width) * 2u * (uint64_t(dstHeight) * 2u);
            if ((dstWidth == 0) || (dstHeight == 0) || (lastByte > RDRAMBytes)) {
                return;
            }

            while (candidates.size() >= MaxCandidates) {
                evictOne(fbManager);
            }

            Candidate candidate;
            candidate.address = address;
            candidate.width = width;
            candidate.dstWidth = dstWidth;
            candidate.dstHeight = dstHeight;
            candidate.pairIndex = pairIndex;
            candidate.tileId = fbManager.findTileCopyId(dstWidth, dstHeight);
            fbManager.tileCopies[candidate.tileId].snapPinned = true;

            FramebufferTile whole = {};
            whole.address = address;
            whole.siz = G_IM_SIZ_16b;
            whole.fmt = G_IM_FMT_RGBA;
            whole.left = 0;
            whole.top = 0;
            whole.right = dstWidth * 2u;
            whole.bottom = dstHeight * 2u;
            whole.lineWidth = dstWidth;
            whole.ditherPattern = 0;
            whole.sourceWidth = width;
            whole.downsampleShift = 1;
            whole.rowOffset = 0;
            whole.wholeImage = 1;
            endOps.emplace_back(fbManager.makeTileCopyTMEM(candidate.tileId, whole));

            if (snapdiag::statsEnabled()) {
                // One line per distinct render size, not per render.
                static std::vector<uint64_t> reported;
                const uint64_t key = (uint64_t(address) << 32) | (uint64_t(width) << 20) | (uint64_t(dstWidth) << 10) | dstHeight;
                bool seen = false;
                for (uint64_t k : reported) {
                    seen = seen || (k == key);
                }

                if (!seen) {
                    reported.push_back(key);
                    printf("[SNAP-PHOTO-DETAIL] pinned render %08X width %u drawn %ux%u -> halved %ux%u as tile copy %llu\n",
                        address, width, drawnWidth, drawnHeight, dstWidth, dstHeight, (unsigned long long)candidate.tileId);
                }
            }

            candidates.push_back(std::move(candidate));
        }

        // Called after the pair's rows were written back to RDRAM: the bytes
        // the CPU halves, so the bitmap it will produce can be computed.
        void fillPixels(const uint8_t *RDRAM, uint32_t pairIndex) {
            for (auto it = candidates.rbegin(); it != candidates.rend(); it++) {
                Candidate &candidate = *it;
                if (candidate.filled || (candidate.pairIndex != pairIndex)) {
                    continue;
                }

                const uint32_t rowBytes = candidate.width * 2u;
                candidate.pixels.resize(size_t(candidate.dstWidth) * candidate.dstHeight);
                for (uint32_t y = 0; y < candidate.dstHeight; y++) {
                    const uint32_t rowA = candidate.address + (2u * y) * rowBytes;
                    const uint32_t rowB = rowA + rowBytes;
                    const uint32_t swap = (y & 1u) ? 4u : 0u;
                    for (uint32_t x = 0; x < candidate.dstWidth; x++) {
                        const uint32_t column = (2u * x) * 2u;
                        const uint16_t value = halve(readHalf(RDRAM, rowA + column), readHalf(RDRAM, rowA + column + 2u),
                            readHalf(RDRAM, rowB + column), readHalf(RDRAM, rowB + column + 2u));
                        const uint32_t byteIndex = ((y * candidate.dstWidth + x) * 2u) ^ swap;
                        if ((byteIndex >> 1) < candidate.pixels.size()) {
                            candidate.pixels[byteIndex >> 1] = value;
                        }
                    }
                }

                candidate.filled = true;

                // The same photo rendered again at the same size (the Report
                // and the Album re-render on a cursor move; the Gallery's
                // enlarge fades a photo it just drew) yields the very bitmap
                // an earlier candidate already holds. Keep the earlier one,
                // which the texture loads may be matching this frame, and
                // release the newcomer, so a re-render never pushes a photo
                // still on screen out of the ring.
                for (auto other = candidates.begin(); other != candidates.end(); other++) {
                    if ((&*other != &candidate) && other->filled && (other->dstWidth == candidate.dstWidth) &&
                        (other->dstHeight == candidate.dstHeight) && (other->pixels == candidate.pixels)) {
                        candidate.duplicate = true;
                        break;
                    }
                }

                return;
            }
        }

        // Called when the display list is done: a candidate whose rows never
        // came back (render-to-RAM off, or the pair was not rendered) can
        // never be matched and is released, as is one that duplicated an
        // earlier candidate's bitmap.
        void endDisplayList(FramebufferManager &fbManager) {
            for (auto it = candidates.begin(); it != candidates.end();) {
                if (!it->filled || it->duplicate) {
                    unpin(fbManager, it->tileId);
                    it = candidates.erase(it);
                }
                else {
                    it++;
                }
            }
        }

        // The oldest render nothing ever matched goes first; failing that,
        // the one whose bitmap was drawn longest ago. Not the oldest render:
        // the Report's page of thumbnails is rendered once and drawn every
        // frame after, while each cursor move renders a fresh preview that
        // is drawn until the next move, so age of creation would evict the
        // thumbnails still on screen ahead of previews long replaced -- the
        // thumbnails then fall back to the console's halved texels.
        void evictOne(FramebufferManager &fbManager) {
            if (candidates.empty()) {
                return;
            }

            auto victim = candidates.end();
            for (auto it = candidates.begin(); it != candidates.end(); it++) {
                if (it->filled && (it->matchedTimestamp == 0)) {
                    victim = it;
                    break;
                }
            }

            if (victim == candidates.end()) {
                victim = candidates.begin();
                for (auto it = candidates.begin(); it != candidates.end(); it++) {
                    if (it->matchedTimestamp < victim->matchedTimestamp) {
                        victim = it;
                    }
                }
            }

            unpin(fbManager, victim->tileId);
            candidates.erase(victim);
        }

        // Whether the bytes a texture load reads are rows of a bitmap the game
        // halved from a pinned render. Newest first.
        bool match(const uint8_t *RDRAM, uint32_t addressStart, uint32_t addressEnd, uint64_t usedTimestamp, Match &out) {
            if ((addressEnd <= addressStart) || (addressEnd > RDRAMBytes) || ((addressStart & 7u) != 0)) {
                return false;
            }

            const uint32_t bytes = addressEnd - addressStart;
            for (auto it = candidates.rbegin(); it != candidates.rend(); it++) {
                Candidate &candidate = *it;
                const uint32_t rowBytes = candidate.dstWidth * 2u;
                // The odd-row swap only stays inside a row when rows are whole
                // 8-byte words; the offsets below assume that.
                if (!candidate.filled || (rowBytes == 0) || ((rowBytes & 7u) != 0) || (bytes < rowBytes) || ((bytes % rowBytes) != 0)) {
                    continue;
                }

                const uint32_t rows = bytes / rowBytes;
                if (rows > candidate.dstHeight) {
                    continue;
                }

                const uint32_t halfwords = bytes / 2u;
                for (uint32_t row = 0; (row + rows) <= candidate.dstHeight; row++) {
                    const uint16_t *expected = candidate.pixels.data() + size_t(row) * candidate.dstWidth;
                    bool same = true;
                    for (uint32_t i = 0; (i < halfwords) && same; i++) {
                        same = (readHalf(RDRAM, addressStart + i * 2u) == expected[i]);
                    }

                    if (same) {
                        if (snapdiag::statsEnabled() && !candidate.matchReported) {
                            candidate.matchReported = true;
                            printf("[SNAP-PHOTO-DETAIL] load %08X..%08X is rows %u..%u of the bitmap halved from render %08X width %u (tile copy %llu)\n",
                                addressStart, addressEnd, row, row + rows, candidate.address, candidate.width, (unsigned long long)candidate.tileId);
                        }

                        candidate.matchedTimestamp = usedTimestamp;
                        out.candidate = &candidate;
                        out.row = row;
                        out.rows = rows;
                        return true;
                    }
                }
            }

            return false;
        }

        // The framebuffer tile a matched load stands for: those rows of the
        // pinned copy. Twice as many rows of the render, read through the
        // 2x2 box, starting rowOffset halved rows into the copy.
        static FramebufferTile makeRegionTile(const Match &match) {
            FramebufferTile tile = {};
            tile.address = match.candidate->address;
            tile.siz = G_IM_SIZ_16b;
            tile.fmt = G_IM_FMT_RGBA;
            tile.left = 0;
            tile.top = match.row * 2u;
            tile.right = match.candidate->dstWidth * 2u;
            tile.bottom = (match.row + match.rows) * 2u;
            tile.lineWidth = match.candidate->dstWidth;
            tile.ditherPattern = 0;
            tile.sourceWidth = match.candidate->width;
            tile.downsampleShift = 1;
            tile.rowOffset = match.row;
            tile.wholeImage = 0;
            return tile;
        }
    };
};
