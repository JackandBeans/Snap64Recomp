/**
 * @file frame_dump.cpp
 * @brief Saves the framebuffers around a churn frame as images.
 *
 * Every measured suspect for the one-frame flash at block transitions has been
 * acquitted by its own probe: the rebase compensations fire, the scenes and
 * transforms pair correctly, per-vertex velocities are disabled, the presents
 * are all interpolated. What has never been examined is the artifact itself.
 * This puts the pixels on disk.
 *
 * It can, because the port runs with render to RAM on: RT64 copies each
 * finished frame back over the framebuffer in RDRAM (that is what the Pokemon
 * detector and the focus dot read), and send_dl blocks until the render is
 * done. So by the time the game checks its buffers for the next frame, the
 * previous frame's final pixels are sitting in memory, and dumping them is a
 * memcpy, not a GPU readback.
 *
 * The renderer arms the dump: on the frames where most of the scene's
 * transforms change identity at once -- the block-transition and spawn frames
 * the flash is reported on -- rt64_workload_queue.cpp sets the pending counter.
 * The game side keeps a short ring of the frames just before, and writes ring
 * plus the following frames as BMPs. If the artifact is in the rendered frame,
 * it will be in the images; if the images are clean, the artifact lives only in
 * the interpolated presents, which is just as decisive.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <direct.h>

namespace snap {
namespace {

// Beach course, from the [SNAP-FBP] logs: the two color buffers the game
// alternates between. A course with different addresses dumps black frames,
// which is itself the answer to whether the addresses were right.
constexpr uint32_t FramebufferAddresses[2] = { 0x803B5000u, 0x803DA800u };
constexpr uint32_t FbWidth = 320;
constexpr uint32_t FbHeight = 240;
constexpr uint32_t FbBytes = FbWidth * FbHeight * 2;

constexpr uint32_t RingSlots = 3;
constexpr uint32_t MaxFilesWritten = 160;

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
bool g_dir_made = false;

// The recompiled memory image stores 32-bit words natively, so a 16-bit read
// at an N64 address lands at the address XOR 2 (the MEM_HU rule). The raw
// bytes are copied as-is into the ring and the XOR is applied when decoding.
uint16_t pixel_at(const uint8_t* raw, uint32_t index) {
    const uint32_t offset = (index * 2) ^ 2;
    uint16_t value;
    std::memcpy(&value, raw + offset, sizeof(value));
    return value;
}

void write_bmp(const char* path, const uint8_t* raw) {
    FILE* f = fopen(path, "wb");
    if (f == nullptr) {
        return;
    }

    const uint32_t rowBytes = FbWidth * 3;
    const uint32_t imageBytes = rowBytes * FbHeight;
    const uint32_t fileBytes = 14 + 40 + imageBytes;
    const uint8_t header[54] = {
        'B', 'M',
        (uint8_t)fileBytes, (uint8_t)(fileBytes >> 8), (uint8_t)(fileBytes >> 16), (uint8_t)(fileBytes >> 24),
        0, 0, 0, 0,
        54, 0, 0, 0,
        40, 0, 0, 0,
        (uint8_t)FbWidth, (uint8_t)(FbWidth >> 8), 0, 0,
        (uint8_t)FbHeight, (uint8_t)(FbHeight >> 8), 0, 0,
        1, 0, 24, 0,
        0, 0, 0, 0,
        (uint8_t)imageBytes, (uint8_t)(imageBytes >> 8), (uint8_t)(imageBytes >> 16), (uint8_t)(imageBytes >> 24),
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    fwrite(header, 1, sizeof(header), f);

    // BMP rows run bottom-up. N64 RGBA16 is 5-5-5-1, red in the top bits.
    static uint8_t row[FbWidth * 3];
    for (int32_t y = FbHeight - 1; y >= 0; y--) {
        for (uint32_t x = 0; x < FbWidth; x++) {
            const uint16_t p = pixel_at(raw, (uint32_t)y * FbWidth + x);
            const uint32_t r = (p >> 11) & 31;
            const uint32_t g = (p >> 6) & 31;
            const uint32_t b = (p >> 1) & 31;
            row[x * 3 + 0] = (uint8_t)((b * 255) / 31);
            row[x * 3 + 1] = (uint8_t)((g * 255) / 31);
            row[x * 3 + 2] = (uint8_t)((r * 255) / 31);
        }
        fwrite(row, 1, sizeof(row), f);
    }
    fclose(f);
}

void dump_slot(const RingSlot& slot, const char* tag) {
    if (!g_dir_made) {
        _mkdir("snap_frame_dumps");
        g_dir_made = true;
    }
    for (uint32_t b = 0; b < 2; b++) {
        if (g_files_written >= MaxFilesWritten) {
            return;
        }
        char path[128];
        snprintf(path, sizeof(path), "snap_frame_dumps/f%06u_%08X_%s.bmp",
                 slot.frame, FramebufferAddresses[b], tag);
        write_bmp(path, slot.pixels[b]);
        g_files_written++;
    }
    printf("[SNAP-DUMP] wrote frame %u (%s)\n", slot.frame, tag);
    fflush(stdout);
}

} // namespace
} // namespace snap

// Armed by the renderer (rt64_workload_queue.cpp) on the frames where most of
// the scene's transform identities change at once. Each game frame dumped
// consumes one count.
extern "C" volatile int32_t snap_frame_dump_pending = 0;

namespace snap {

// Called once per game frame from the gtlCheckBuffers hook. The frame in RDRAM
// at that point is the last one the renderer finished, because send_dl waits
// for the workload before returning.
void frame_dump_tick(uint8_t* rdram) {
    g_frame++;

    RingSlot& slot = g_ring[g_ring_next];
    slot.frame = g_frame;
    slot.valid = true;
    for (uint32_t b = 0; b < 2; b++) {
        std::memcpy(slot.pixels[b], rdram + (FramebufferAddresses[b] - 0x80000000u), FbBytes);
    }

    const int32_t pending = snap_frame_dump_pending;
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
        snap_frame_dump_pending = pending - 1;
    }
    g_last_pending = pending;

    g_ring_next = (g_ring_next + 1) % RingSlots;
}

} // namespace snap
