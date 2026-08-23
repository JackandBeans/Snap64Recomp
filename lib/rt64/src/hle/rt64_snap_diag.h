/**
 * @file rt64_snap_diag.h
 * @brief Shared pieces of the Pokemon Snap port's diagnostics.
 *
 * Two things live here, both born from the code review of the transition-cut
 * work.
 *
 * The gate: the port accumulated a family of [SNAP-*] printf probes across
 * the renderer while the corner flash was being hunted. Left always-on they
 * cost formatting and flushes in hot paths and they are release-cleanup debt
 * scattered across a vendored fork. Every probe family now asks diagEnabled()
 * first, so shipping quiet builds is one environment variable, and stripping
 * the probes later is a search for one symbol.
 *
 * The dump helpers: both capture rigs (the game-side RDRAM dumper in
 * src/frame_dump.cpp and the present-queue capture) previously hand-rolled
 * their own BMP writer, directory creation, and file numbering. The writers
 * had diverging row-padding rules and both counted failed writes as
 * successes. One implementation now serves both, reports failure honestly,
 * and stamps every filename with a per-run token so a later run can never
 * silently overwrite an earlier run's evidence.
 */

#pragma once

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <direct.h>

namespace snapdiag {

// One switch for every [SNAP-*] diagnostic probe family.
inline bool diagEnabled() {
    static const bool enabled = (std::getenv("SNAP_DIAG") != nullptr);
    return enabled;
}

// Low-overhead measurement: the census line alone, no per-frame walks, no
// flushes, no captures. The heavy probes perturb frame pacing enough to
// change the very timing-sensitive behavior they are measuring, so numbers
// meant to describe how the game actually runs are gathered under this.
inline bool statsEnabled() {
    static const bool enabled = (std::getenv("SNAP_STATS") != nullptr);
    return enabled;
}

// One switch for both frame-capture rigs.
inline bool captureEnabled() {
    static const bool enabled = (std::getenv("SNAP_CAPTURE") != nullptr);
    return enabled;
}

// Counts frames the renderer held instead of showing, so the pacing meter can
// say whether a stutter is the hold system's own doing. A hold copies a whole
// render target and waits for the GPU to finish, which is real time spent on
// exactly the frames that are already the heaviest in the scene.
inline std::atomic<uint32_t> &holdCounter() {
    static std::atomic<uint32_t> counter{0};
    return counter;
}

// Distinguishes this run's files from every other run's. Derived from the
// launch time, so re-running an experiment adds files instead of replacing
// the evidence the previous run produced.
inline uint32_t runToken() {
    static const uint32_t token = uint32_t(time(nullptr) % 100000u);
    return token;
}

// Creates the dump directory once. A failed creation is remembered as a
// failure, not silently latched as success the way both rigs used to.
inline bool ensureDumpDir() {
    static int state = 0;
    if (state == 0) {
        const int result = _mkdir("snap_frame_dumps");
        state = ((result == 0) || (errno == EEXIST)) ? 1 : -1;
    }
    return state == 1;
}

// Writes a 24-bit BMP from a top-down, tightly packed BGR buffer. Returns
// whether the file was actually produced, so callers can count and report
// honestly.
inline bool writeBMP24(const char *path, uint32_t width, uint32_t height, const uint8_t *bgrTopDown) {
    if ((width == 0) || (height == 0) || (bgrTopDown == nullptr)) {
        return false;
    }

    FILE *f = fopen(path, "wb");
    if (f == nullptr) {
        return false;
    }

    const uint32_t rowBytes = ((width * 3) + 3) & ~3u;
    const uint32_t imageBytes = rowBytes * height;
    const uint32_t fileBytes = 54 + imageBytes;
    const uint8_t header[54] = {
        'B', 'M',
        uint8_t(fileBytes), uint8_t(fileBytes >> 8), uint8_t(fileBytes >> 16), uint8_t(fileBytes >> 24),
        0, 0, 0, 0,
        54, 0, 0, 0,
        40, 0, 0, 0,
        uint8_t(width), uint8_t(width >> 8), uint8_t(width >> 16), 0,
        uint8_t(height), uint8_t(height >> 8), uint8_t(height >> 16), 0,
        1, 0, 24, 0,
        0, 0, 0, 0,
        uint8_t(imageBytes), uint8_t(imageBytes >> 8), uint8_t(imageBytes >> 16), uint8_t(imageBytes >> 24),
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };

    bool ok = (fwrite(header, 1, sizeof(header), f) == sizeof(header));

    // BMP rows run bottom-up; pad each packed row out to four bytes.
    const uint8_t padding[3] = {};
    const uint32_t padBytes = rowBytes - width * 3;
    for (int32_t y = int32_t(height) - 1; ok && (y >= 0); y--) {
        ok = (fwrite(bgrTopDown + uint64_t(y) * width * 3, 1, width * 3, f) == width * 3);
        if (ok && (padBytes > 0)) {
            ok = (fwrite(padding, 1, padBytes, f) == padBytes);
        }
    }

    fclose(f);
    return ok;
}

} // namespace snapdiag
