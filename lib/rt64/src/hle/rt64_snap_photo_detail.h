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
// What a window or aspect change does. The renderer destroys every tile copy
// when the window's size or the frame's configuration changes (rt64_workload_
// queue.cpp, threadConfigurationUpdate), and the game does not render its
// photos again for that: a Gallery page drawn from pinned copies that were
// gone showed whatever GPU tile the missing copy's id fell on (issue #12,
// Succulent-Puppet: wrong thumbnails after a switch to fullscreen). The
// pinned copies now outlive that wipe (rt64_framebuffer_manager.cpp,
// destroyAllTileCopies); each stands on its own texture, so the thumbnails
// keep their detail. A copy made before the wipe is at the old render
// scale; when the game renders the same photo again the newer copy is kept
// and the older one released, where an unchanged scale keeps the older.
//
// Off, none of this runs: no pin, no note, no comparison.
//

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
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
            // How many tile-copy wipes the renderer had done when this was
            // pinned (FramebufferManager::snapTileCopyWipes). The copy itself
            // survives a wipe; the count tells a re-render of the same photo
            // after a window change from one before it (see fillPixels).
            uint64_t generation = 0;
            // The display list this was pinned in (listCounter then): its
            // sprite's loads may come only in the next one, after the list
            // that filled it has ended, so until then it is not evictable.
            uint64_t createdList = 0;
            bool filled = false;
            bool duplicate = false;
            uint64_t matchedTimestamp = 0;
            // The display list it was last matched in: a photo drawn every
            // frame is matched every list, and one matched within the last
            // two lists is on screen and never evicted.
            uint64_t matchedList = 0;
            // The load range that last matched this candidate: the sprite
            // bitmap its photo was halved into. When a load at that very
            // range matches another candidate, the game has halved another
            // photo into the bitmap, and this one's content is gone from
            // memory for good: superseded, the first to leave the ring.
            uint32_t matchStart = 0;
            uint32_t matchEnd = 0;
            bool superseded = false;
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
        // The smallest photo the game halves is the 52x39 thumbnail (from
        // a 104x78 render). Renders below this, halved, are the interface's
        // own: 8x8 and 16x16 icons, 32x12 list entries, rendered by the
        // dozen every frame. Pinning those flooded the ring within a single
        // frame and pushed the photos being drawn out of it (the author's
        // playtest log, 2026-09-15: "evicted a matched photo" 48 times in
        // one session, the big photo and the thumbnails alike).
        static constexpr uint32_t MinHalvedWidth = 40;
        static constexpr uint32_t MinHalvedHeight = 30;
        // Enough for every photo a screen shows at once, the previews the
        // Report renders afresh on every cursor move, and the scoring passes
        // between them; each pin is the halved photo at the render scale,
        // half a megabyte for a thumbnail at 8x.
        // 64: a course's eighteen photos, the Report's page and preview,
        // Oak's check and the comparison after it, with room to spare. The
        // game keeps a photo's bitmap across screens without rendering it
        // again -- the Camera Check's thumbnail is what Oak's comparison
        // draws as "This time" -- so a copy must outlive the screen it was
        // made on; 40, with the Report's previews churning through it, lost
        // the course's photos before Oak's comparison showed them (my
        // playtest log, 2026-09-15).
        static constexpr size_t MaxCandidates = 64;
        static constexpr uint32_t RDRAMBytes = 0x800000;
        // A VI origin sits a row or two into its buffer; treat anything within
        // a few rows of a displayed address as the screen.
        static constexpr uint32_t ScreenTolerance = 4096;

        std::deque<Candidate> candidates;
        // Counts the display lists ended (endDisplayList); the age of a
        // candidate in lists is what eviction goes by.
        uint64_t listCounter = 0;

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
                (drawnWidth >= 2) && (drawnWidth <= width) && (drawnHeight >= 2) && (drawnHeight <= MaxSourceHeight) &&
                ((drawnWidth >> 1) >= MinHalvedWidth) && ((drawnHeight >> 1) >= MinHalvedHeight);
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

            while ((candidates.size() >= MaxCandidates) && evictOne(fbManager)) {
            }

            Candidate candidate;
            candidate.generation = FramebufferManager::snapTileCopyWipes.load(std::memory_order_acquire);
            candidate.createdList = listCounter;
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
                // still on screen out of the ring. Unless the earlier one was
                // pinned before a window change: its copy is at the render
                // scale of that time, the newcomer's at this one's, so the
                // newcomer stays and the earlier one is released instead
                // (its copy is drawn from until the end of this display list
                // at most, and the loads from then on match the newcomer,
                // the newest first).
                for (auto other = candidates.begin(); other != candidates.end(); other++) {
                    if ((&*other != &candidate) && other->filled && !other->duplicate && (other->dstWidth == candidate.dstWidth) &&
                        (other->dstHeight == candidate.dstHeight) && (other->pixels == candidate.pixels)) {
                        if (other->generation != candidate.generation) {
                            other->duplicate = true;
                        }
                        else {
                            candidate.duplicate = true;
                        }

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

            listCounter++;
        }

        // The oldest render nothing ever matched goes first; failing that,
        // the one whose bitmap was drawn longest ago. Not the oldest render:
        // the Report's page of thumbnails is rendered once and drawn every
        // frame after, while each cursor move renders a fresh preview that
        // is drawn until the next move, so age of creation would evict the
        // thumbnails still on screen ahead of previews long replaced -- the
        // thumbnails then fall back to the console's halved texels.
        //
        // Never one pinned in this display list or the last (createdList):
        // its rows come back at a flush, and a sprite whose loads ran before
        // that flush finds the copy only in the next list. Before this rule
        // such a copy counted as "filled and never matched", the first to
        // go when the ring was full, and on a screen that renders enough
        // small things a frame the ring was full every frame: the copy was
        // pinned, filled after the loads, evicted at the next render, and
        // its photo drew from the console's texels for as long as the
        // screen lasted. A render split in two by a framebuffer read (the
        // fire in a Volcano photo) gave it one copy per part: the first,
        // filled before the loads, served the top strips; the second never
        // served, and the bottom of the photo stayed pixelated (my own
        // Charizard review, and issue #15's Cave thumbnails, 2026-09-14).
        //
        // And never one matched in this display list or the last: that is a
        // photo on screen, drawn every frame. If nothing else is left the
        // ring grows past MaxCandidates rather than lose a photo the player
        // is looking at; with the interface's small renders kept out
        // (MinHalvedWidth) it holds a screen's photos with room to spare.
        // Returns false when nothing may go.
        bool evictOne(FramebufferManager &fbManager) {
            if (candidates.empty()) {
                return false;
            }

            auto victim = candidates.end();
            const auto evictable = [&](const Candidate &c) {
                const bool young = (c.createdList + 1) >= listCounter;
                const bool onScreen = (c.matchedTimestamp != 0) && ((c.matchedList + 1) >= listCounter);
                return !young && !onScreen;
            };

            // Superseded first: its photo was halved over in memory and no
            // load can match it again.
            for (auto it = candidates.begin(); it != candidates.end(); it++) {
                if (evictable(*it) && it->superseded) {
                    victim = it;
                    break;
                }
            }

            if (victim == candidates.end()) {
                for (auto it = candidates.begin(); it != candidates.end(); it++) {
                    if (evictable(*it) && it->filled && (it->matchedTimestamp == 0)) {
                        victim = it;
                        break;
                    }
                }
            }

            if (victim == candidates.end()) {
                for (auto it = candidates.begin(); it != candidates.end(); it++) {
                    if (evictable(*it) && ((victim == candidates.end()) || (it->matchedTimestamp < victim->matchedTimestamp))) {
                        victim = it;
                    }
                }
            }

            if (victim == candidates.end()) {
                static int reported = 0;
                if (reported < 20) {
                    reported++;
                    printf("[SNAP-PHOTO-DETAIL] the ring holds %u photos on screen or just rendered; it grows rather than evict one\n",
                        unsigned(candidates.size()));
                }

                return false;
            }

            if ((victim->matchedTimestamp != 0) && !victim->superseded) {
                // A photo that was drawn from, two lists ago or more, whose
                // bitmap may still come back: going for lack of room.
                static int reported = 0;
                if (reported < 50) {
                    reported++;
                    printf("[SNAP-PHOTO-DETAIL] evicted a photo last drawn from in list %llu of %llu (render %08X, %ux%u) for a new render\n",
                        (unsigned long long)victim->matchedList, (unsigned long long)listCounter, victim->address, victim->dstWidth, victim->dstHeight);
                }
            }

            unpin(fbManager, victim->tileId);
            candidates.erase(victim);
            return true;
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
                if (candidate.dstHeight < 2) {
                    continue;
                }

                // Every window that fits whole first; then the tail, where a
                // strip runs past the bitmap's end and only the rows that
                // exist are compared, two at least. The last strip of a
                // 105-row photo is loaded as fourteen rows of which seven
                // exist (my playtest log, 2026-09-15: the load at the eighth
                // strip's address matched nothing at any row, six frames
                // running); the tile then stands for the rows that exist,
                // and the sprite draws no further than them.
                const bool anyFull = (rows <= candidate.dstHeight);
                const uint32_t fullLast = anyFull ? (candidate.dstHeight - rows) : 0;
                for (uint32_t row = 0; row < candidate.dstHeight; row++) {
                    const bool full = anyFull && (row <= fullLast);
                    const uint32_t avail = full ? rows : (candidate.dstHeight - row);
                    if (avail < 2) {
                        break;
                    }

                    const uint32_t halfwords = avail * candidate.dstWidth;
                    const uint16_t *expected = candidate.pixels.data() + size_t(row) * candidate.dstWidth;
                    bool same = true;
                    for (uint32_t i = 0; (i < halfwords) && same; i++) {
                        same = (readHalf(RDRAM, addressStart + i * 2u) == expected[i]);
                    }

                    if (same) {
                        if (snapdiag::statsEnabled() || (candidate.dstWidth >= 100)) {
                            // Each distinct load range and match once: which
                            // rows of which bitmap each strip was served from.
                            // Always on for the big photos, a line per strip
                            // per photo, so a log shows what was served.
                            static std::vector<uint64_t> seen;
                            const uint64_t key = (uint64_t(addressStart) << 32) ^ (uint64_t(addressEnd) << 8) ^ (uint64_t(candidate.address) << 20) ^ row;
                            bool known = false;
                            for (uint64_t k : seen) {
                                known = known || (k == key);
                            }

                            if (!known && (seen.size() < 400)) {
                                seen.push_back(key);
                                printf("[SNAP-PHOTO-DETAIL] load %08X..%08X is rows %u..%u of the bitmap halved from render %08X width %u (tile copy %llu)%s\n",
                                    addressStart, addressEnd, row, row + avail, candidate.address, candidate.width, (unsigned long long)candidate.tileId,
                                    full ? "" : ", the rest past its end");
                            }
                        }

                        candidate.matchReported = true;
                        candidate.matchedTimestamp = usedTimestamp;
                        candidate.matchedList = listCounter;
                        // The bitmap now holds this candidate's photo: any
                        // other candidate this range last matched is gone.
                        for (Candidate &other : candidates) {
                            if ((&other != &candidate) && (other.matchStart == addressStart) && (other.matchEnd == addressEnd)) {
                                other.superseded = true;
                            }
                        }

                        candidate.matchStart = addressStart;
                        candidate.matchEnd = addressEnd;
                        out.candidate = &candidate;
                        out.row = row;
                        out.rows = avail;
                        return true;
                    }
                }
            }

            {
                // A plausible strip that matched nothing: the candidate and
                // row it came closest to, and the first halfword that broke
                // the match, so a missed strip says why. Always on, each
                // distinct miss once, a hundred at most: a healthy run
                // writes none of these.
                static std::vector<uint64_t> missedSeen;
                if ((bytes >= 1000) && (missedSeen.size() < 100)) {
                    uint32_t bestLen = 0;
                    uint32_t bestRow = 0;
                    const Candidate *best = nullptr;
                    uint16_t bestGot = 0;
                    uint16_t bestWant = 0;
                    for (auto it = candidates.rbegin(); it != candidates.rend(); it++) {
                        const Candidate &candidate = *it;
                        const uint32_t rowBytes = candidate.dstWidth * 2u;
                        if (!candidate.filled || (rowBytes == 0) || ((rowBytes & 7u) != 0) || (bytes < rowBytes) || ((bytes % rowBytes) != 0)) {
                            continue;
                        }

                        // Only a load that could be this photo: its whole
                        // bitmap, or a strip of a big one. A sprite of a
                        // coincidental width (72x26 list art loads as 36
                        // rows of 52) is not worth a line.
                        const bool whole = (bytes == rowBytes * candidate.dstHeight);
                        const bool strip = (candidate.dstWidth >= 100) && (bytes <= rowBytes * candidate.dstHeight);
                        if (!whole && !strip) {
                            continue;
                        }

                        const uint32_t rows = bytes / rowBytes;
                        if (candidate.dstHeight < 2) {
                            continue;
                        }

                        for (uint32_t row = 0; row < candidate.dstHeight; row++) {
                            const uint32_t avail = std::min(rows, candidate.dstHeight - row);
                            if (avail < 2) {
                                break;
                            }

                            const uint32_t halfwords = avail * candidate.dstWidth;
                            const uint16_t *expected = candidate.pixels.data() + size_t(row) * candidate.dstWidth;
                            uint32_t i = 0;
                            while ((i < halfwords) && (readHalf(RDRAM, addressStart + i * 2u) == expected[i])) {
                                i++;
                            }

                            if ((i > bestLen) || (best == nullptr)) {
                                bestLen = i;
                                bestRow = row;
                                best = &candidate;
                                bestGot = (i < halfwords) ? readHalf(RDRAM, addressStart + i * 2u) : 0;
                                bestWant = (i < halfwords) ? expected[i] : 0;
                            }
                        }
                    }

                    const uint64_t key = (uint64_t(addressStart) << 32) ^ (uint64_t(addressEnd) << 8) ^ (uint64_t(bestLen) << 40) ^ bestRow;
                    bool known = false;
                    for (uint64_t k : missedSeen) {
                        known = known || (k == key);
                    }

                    if ((best != nullptr) && !known) {
                        missedSeen.push_back(key);
                        unsigned unfilled = 0;
                        for (const Candidate &c : candidates) {
                            unfilled += c.filled ? 0u : 1u;
                        }
                        printf("[SNAP-PHOTO-DETAIL] miss %08X..%08X (%u bytes): closest is render %08X (%ux%u, list %llu of %llu) at row %u, %u of %u halfwords equal, then %04X in memory vs %04X computed; %u candidates, %u not yet filled\n",
                            addressStart, addressEnd, bytes, best->address, best->dstWidth, best->dstHeight, (unsigned long long)best->createdList,
                            (unsigned long long)listCounter, bestRow, bestLen, bytes / 2u, bestGot, bestWant, unsigned(candidates.size()), unfilled);
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
