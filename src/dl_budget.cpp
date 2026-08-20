/**
 * @file dl_budget.cpp
 * @brief Watches how full the game's display list buffers get.
 *
 * The port writes more display list than the game does. Object identity for
 * interpolation is a gEXMatrixGroup command emitted ahead of every matrix, two
 * words each, and a scene with a few hundred matrices in it therefore carries a
 * few kilobytes that the original never had. The buffers those go into are
 * small and fixed: a course sets dlBufferSize0 to 0x5000 or 0x5400, which is
 * 20KB, about 2560 commands for the entire frame.
 *
 * The game already checks this. gtlCheckBuffers runs once a frame and compares
 * each buffer's write pointer against its end, and a course that overruns is a
 * PANIC on real hardware. What it does not do is say how close it came, which
 * is the number that matters when deciding whether the extra commands fit. So
 * this reports the high water mark, and says so loudly if a buffer is ever
 * exceeded.
 *
 * Worth knowing because an overrun does not fail cleanly. Nothing bounds-checks
 * during the frame; the game writes straight past the end into whatever
 * gtlMalloc handed out next, which is the neighbouring display list buffer and
 * then the matrix heap. Geometry emitted late in the frame would be the part
 * that lands outside, and the matrices it refers to would be the ones
 * overwritten.
 */

#include <cstdarg>
#include <cstdint>
#include <cstdio>

#include "recomp.h"

extern "C" {
#include "funcs.h"
}

namespace snap {
namespace {

// From patches/game_syms.ld, which is generated from the decompilation.
constexpr uint32_t GtlDLBuffers      = 0x8004A850;  // DLBuffer[context][kind]
constexpr uint32_t GMainGfxPos       = 0x8004A890;  // Gfx*[kind], the write pointers
constexpr uint32_t GtlCurrentGfxHeap = 0x8004A8B8;  // DynamicBuffer: id, start, end, ptr
constexpr uint32_t GtlContextId      = 0x8004A910;

constexpr uint32_t DLBufferSize = 8;    // Gfx* start; s32 length
constexpr uint32_t BufferKinds  = 4;

// MEM_W resolves against a pointer named rdram, so it is threaded through.
uint32_t read_word(uint8_t* rdram, uint32_t address) {
    return MEM_W(0, (gpr)(int32_t)address);
}

// Written to a file as well as stdout: the port is a windowed application, so
// asking someone to catch console output to answer a question about a graphical
// artifact is a worse experiment than leaving the answer on disk.
void report(const char* format, ...) {
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    vprintf(format, args);
    fflush(stdout);
    if (FILE* log = fopen("snap_dl_budget.log", "a")) {
        vfprintf(log, format, copy);
        fclose(log);
    }
    va_end(copy);
    va_end(args);
}

// Reported per kind so a buffer that is merely full is distinguishable from one
// that is being overrun, and so the margin is visible before it runs out.
uint32_t g_peak_used[BufferKinds] = {};
uint32_t g_peak_heap = 0;
bool     g_overflowed[BufferKinds] = {};
bool     g_heap_overflowed = false;

} // namespace

// Defined below, called from the per-frame check.
void watch_blobs(uint8_t* rdram);
} // namespace snap

extern "C" void gtlCheckBuffers(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t context = snap::read_word(rdram, snap::GtlContextId);

    for (uint32_t kind = 0; kind < snap::BufferKinds; kind++) {
        const uint32_t entry = snap::GtlDLBuffers + ((context * snap::BufferKinds) + kind) * snap::DLBufferSize;
        const uint32_t start    = snap::read_word(rdram, entry);
        const uint32_t capacity = snap::read_word(rdram, entry + 4);
        const uint32_t pos      = snap::read_word(rdram, snap::GMainGfxPos + (kind * 4));

        if ((capacity == 0) || (start == 0) || (pos < start)) {
            continue;
        }

        // Quiet unless a buffer is running out. The port emits more display
        // list than the game does, so the margin is worth watching, but it was
        // measured at 57.5% of the main buffer and there is nothing to report
        // frame by frame.
        const uint32_t used = pos - start;
        const uint32_t reportThreshold = (capacity / 10) * 9;
        if ((used > snap::g_peak_used[kind]) && (used >= reportThreshold)) {
            snap::g_peak_used[kind] = used;
            printf("[SNAP-DL] kind %u peak %u / %u bytes (%.1f%% full, %d spare)\n",
                   kind, used, capacity, (100.0 * used) / capacity,
                   (int32_t)capacity - (int32_t)used);
            fflush(stdout);
        }

        // The game PANICs here. Say it plainly instead, once per kind: past the
        // end the writes have already happened, into the next allocation.
        if ((used > capacity) && !snap::g_overflowed[kind]) {
            snap::g_overflowed[kind] = true;
            printf("[SNAP-DL] *** OVERFLOW *** kind %u wrote %u bytes into a %u byte buffer, %u past the end\n",
                   kind, used, capacity, used - capacity);
            fflush(stdout);
        }
    }

    const uint32_t heap_start = snap::read_word(rdram, snap::GtlCurrentGfxHeap + 0x4);
    const uint32_t heap_end   = snap::read_word(rdram, snap::GtlCurrentGfxHeap + 0x8);
    const uint32_t heap_ptr   = snap::read_word(rdram, snap::GtlCurrentGfxHeap + 0xC);
    if ((heap_start != 0) && (heap_end > heap_start) && (heap_ptr >= heap_start)) {
        const uint32_t used     = heap_ptr - heap_start;
        const uint32_t capacity = heap_end - heap_start;
        const uint32_t heapThreshold = (capacity / 10) * 9;
        if ((used > snap::g_peak_heap) && (used >= heapThreshold)) {
            snap::g_peak_heap = used;
            printf("[SNAP-DL] matrix heap peak %u / %u bytes (%.1f%% full)\n",
                   used, capacity, (100.0 * used) / capacity);
            fflush(stdout);
        }
        if ((heap_ptr > heap_end) && !snap::g_heap_overflowed) {
            snap::g_heap_overflowed = true;
            printf("[SNAP-DL] *** OVERFLOW *** matrix heap wrote %u bytes into a %u byte heap\n", used, capacity);
            fflush(stdout);
        }
    }

    snap::watch_blobs(rdram);

    __real_gtlCheckBuffers(rdram, ctx);
}

// The effect sprite blobs: recorded when the game loads one, re-checked every
// frame from the buffer check above, so the log shows the exact frame the
// content changes. The sprites were proven to be zeros at draw time while
// their palettes survive, and the load path reads correctly at boot, so either
// the load itself fails at course start or something overwrites the blob
// afterwards -- and the transition timing says which, and when.
namespace snap {
namespace {
struct WatchedBlob {
    uint32_t address = 0;
    uint32_t size = 0;
    bool hadData = false;
};
WatchedBlob g_blobs[8];
uint32_t g_blob_count = 0;
uint32_t g_watch_frame = 0;

bool blob_has_data(uint8_t* rdram, uint32_t address) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < 32; i += 4) {
        sum |= MEM_W(i, (gpr)(int32_t)address);
    }
    return sum != 0;
}
} // namespace
} // namespace snap

// a0 = ROM start, a1 = ROM end; v0 = destination the blob was loaded to.
extern "C" void func_800A73C0(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t romStart = (uint32_t)ctx->r4;
    const uint32_t romEnd = (uint32_t)ctx->r5;

    __real_func_800A73C0(rdram, ctx);

    const uint32_t dst = (uint32_t)ctx->r2;
    if (dst != 0 && snap::g_blob_count < 8) {
        snap::WatchedBlob& blob = snap::g_blobs[snap::g_blob_count++];
        blob.address = dst;
        blob.size = romEnd - romStart;
        blob.hadData = snap::blob_has_data(rdram, dst);
        printf("[SNAP-BLOB] loaded rom %08X..%08X -> %08X (%u bytes) data %u\n",
               romStart, romEnd, dst, blob.size, blob.hadData ? 1u : 0u);
        fflush(stdout);
    }
}

namespace snap {
void watch_blobs(uint8_t* rdram) {
    g_watch_frame++;
    for (uint32_t i = 0; i < g_blob_count; i++) {
        WatchedBlob& blob = g_blobs[i];
        const bool has = blob_has_data(rdram, blob.address);
        if (has != blob.hadData) {
            printf("[SNAP-BLOB] %08X changed to %s at frame %u\n",
                   blob.address, has ? "DATA" : "ZERO", g_watch_frame);
            fflush(stdout);
            blob.hadData = has;
        }
    }
}
} // namespace snap
