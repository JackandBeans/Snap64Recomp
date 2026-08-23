/**
 * @file frame_dump.cpp
 * @brief Saves the framebuffers around a churn frame as images.
 *
 * The renderer arms the dump: on the frames where most of the scene's
 * transforms change identity at once, rt64_workload_queue.cpp raises the
 * pending counter, and this side keeps a short ring of the frames just before
 * so the moment is bracketed. The port runs with render to RAM on, so the
 * previous frame's final pixels are sitting in RDRAM by the time the game
 * checks its buffers; dumping them is a memcpy, not a GPU readback.
 *
 * Everything here is inert unless SNAP_CAPTURE is set: the review measured
 * the ring copies at ~307KB per game frame, which nobody should pay for a
 * diagnostic that cannot fire. The pending counter is atomic because three
 * threads touch it (the render thread arms it, this game-thread side consumes
 * it, the present queue reads it), and a torn read-modify-write here was
 * shown to be able to cancel the very burst being captured. Files are
 * written through the shared writer in rt64_snap_diag.h, which reports
 * failure instead of counting it as success, and every name carries the
 * run token so consecutive runs never overwrite each other's evidence.
 */

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "hle/rt64_snap_diag.h"

namespace snap {
namespace {

// Beach course, from the [SNAP-FBP] logs: the two color buffers the game
// alternates between. The same addresses are used across the retail courses'
// display lists; a course that deviates dumps stale memory, which is itself
// visible in the images.
constexpr uint32_t FramebufferAddresses[2] = { 0x803B5000u, 0x803DA800u };
constexpr uint32_t FbWidth = 320;
constexpr uint32_t FbHeight = 240;
constexpr uint32_t FbBytes = FbWidth * FbHeight * 2;

constexpr uint32_t RingSlots = 90;
constexpr uint32_t MaxFilesWritten = 1200;

struct RingSlot {
    uint32_t frame = 0;
    bool valid = false;
    uint8_t pixels[2][FbBytes];
};

RingSlot g_ring[RingSlots];
uint32_t g_ring_next = 0;
uint32_t g_frame = 0;
uint32_t g_files_written = 0;
int32_t g_last_pending = 0;

// The recompiled memory image stores 32-bit words natively, so a 16-bit read
// at an N64 address lands at the address XOR 2 (the MEM_HU rule). The raw
// bytes are copied as-is into the ring and the XOR is applied when decoding.
uint16_t pixel_at(const uint8_t* raw, uint32_t index) {
    const uint32_t offset = (index * 2) ^ 2;
    uint16_t value;
    std::memcpy(&value, raw + offset, sizeof(value));
    return value;
}

// N64 RGBA16 is 5-5-5-1, red in the top bits; the shared writer wants a
// top-down packed BGR buffer.
bool write_frame(const char* path, const uint8_t* raw) {
    static uint8_t bgr[FbWidth * FbHeight * 3];
    for (uint32_t y = 0; y < FbHeight; y++) {
        for (uint32_t x = 0; x < FbWidth; x++) {
            const uint16_t p = pixel_at(raw, y * FbWidth + x);
            const uint32_t r = (p >> 11) & 31;
            const uint32_t g = (p >> 6) & 31;
            const uint32_t b = (p >> 1) & 31;
            uint8_t* out = &bgr[(y * FbWidth + x) * 3];
            out[0] = (uint8_t)((b * 255) / 31);
            out[1] = (uint8_t)((g * 255) / 31);
            out[2] = (uint8_t)((r * 255) / 31);
        }
    }
    return snapdiag::writeBMP24(path, FbWidth, FbHeight, bgr);
}

void dump_slot(const RingSlot& slot, const char* tag) {
    if (!snapdiag::ensureDumpDir()) {
        return;
    }
    uint32_t wrote = 0;
    for (uint32_t b = 0; b < 2; b++) {
        if (g_files_written >= MaxFilesWritten) {
            return;
        }
        char path[160];
        snprintf(path, sizeof(path), "snap_frame_dumps/r%05u_f%06u_%08X_%s.bmp",
                 snapdiag::runToken(), slot.frame, FramebufferAddresses[b], tag);
        if (write_frame(path, slot.pixels[b])) {
            g_files_written++;
            wrote++;
        }
    }
    if (wrote > 0) {
        printf("[SNAP-DUMP] wrote frame %u (%s, %u files)\n", slot.frame, tag, wrote);
        fflush(stdout);
    }
}

} // namespace
} // namespace snap

// Armed by the renderer (rt64_workload_queue.cpp) on the frames where most of
// the scene's transform identities change at once. Each game frame dumped
// consumes one count. Atomic: the render thread stores fresh arms while this
// side decrements, and the present queue polls it for its own captures.
extern "C" std::atomic<int32_t> snap_frame_dump_pending{0};

namespace snap {

// Called once per game frame from the gtlCheckBuffers hook. The frame in RDRAM
// at that point is the last one the renderer finished, because send_dl waits
// for the workload before returning.
void frame_dump_tick(uint8_t* rdram) {
    if (!snapdiag::captureEnabled()) {
        return;
    }

    g_frame++;

    RingSlot& slot = g_ring[g_ring_next];
    slot.frame = g_frame;
    slot.valid = true;
    for (uint32_t b = 0; b < 2; b++) {
        std::memcpy(slot.pixels[b], rdram + (FramebufferAddresses[b] - 0x80000000u), FbBytes);
    }

    const int32_t pending = snap_frame_dump_pending.load();
    if (pending > 0) {
        // First frame of a burst: flush the frames that came before it.
        if (g_last_pending <= 0) {
            for (uint32_t i = 1; i < RingSlots; i++) {
                const RingSlot& old = g_ring[(g_ring_next + i) % RingSlots];
                if (old.valid) {
                    dump_slot(old, "before");
                }
            }
        }
        dump_slot(slot, "at");
        // fetch_sub instead of a stored 'pending - 1': a fresh arm landing
        // between the load above and this line must not be clobbered to zero.
        snap_frame_dump_pending.fetch_sub(1);
    }
    g_last_pending = pending;

    g_ring_next = (g_ring_next + 1) % RingSlots;
}

} // namespace snap
