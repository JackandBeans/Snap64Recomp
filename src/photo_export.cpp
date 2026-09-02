/**
 * @file photo_export.cpp
 * @brief See photo_export.h.
 *
 * Where the picture is. The window library (decomp src/window/847B60.c,
 * overlay section 108 at 0x80369F80) allocates one colour buffer and one
 * depth buffer of 0x20D00 bytes each at start-up, and its data words at
 * 0x803A6660/0x803A6664 -- the width and height every render passes to
 * renInitCameraEx -- are 0x140 and 0xD2: the buffer is 320x210 RGBA16.
 * (0x20D00 also factorises as 280x240; the game's own words settle it.) A
 * photo is rendered into it through func_80374608_847DB8, which clamps the
 * requested size to 16..320 by 16..210, sets the camera's viewport to that
 * rectangle at the top-left corner, and draws once through gtlDrawOne, which
 * waits for the RDP before it returns. The recompiled tree has exactly one
 * caller of renInitCameraEx, that library's camera callback, and the callers
 * of the library's display service are Oak's check, the album, the report,
 * the picks after a course, and the credits.
 *
 * So what is saved is the viewport rectangle, [x0,x1) by [y0,y1), computed
 * here from the camera's Vp exactly as renInitCameraEx computes its scissor:
 * vtrans/4 -/+ vscale/4 with each quotient truncated on its own, clamped to
 * the buffer. The rest of the buffer is whatever the heap held and is not
 * part of the photo.
 *
 * The pixel format is RGBA5551 in big-endian halfwords; the recompiler
 * stores halfwords at address ^ 2, and MEM_HU undoes that. The low bit of a
 * framebuffer pixel is the coverage bit, not alpha -- the background fill
 * sets it and anti-aliased edges clear it -- so the PNG is RGB: a photo with
 * transparent holes along every silhouette would not be the photo.
 *
 * The course name in the file name is taken only when it is certain: the
 * library's own buffer variable (0x803A6C10) must name the buffer being
 * rendered, and then its PhotoData pointer (0x803A667C) is the photo being
 * drawn, whose first word carries levelID in its top seven bits (the game
 * reads it as word >> 25; funcs_11.c, before getLevelName). Otherwise the
 * name simply has no course in it.
 */

#include "photo_export.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <new>
#include <string>
#include <system_error>
#include <vector>

#include "hle/rt64_snap_diag.h"
#include "paths.h"
#include "settings.h"

// The encoder. STATIC keeps the implementation private to this file, so a
// second copy compiled anywhere else can never collide with it; NO_STDIO
// drops stb's own fopen path, which would hand a narrow name to the C
// runtime and lose a non-ASCII install path on Windows -- the file is opened
// here, through std::filesystem::path. The remaining warning is MSVC's
// deprecation of sprintf inside the HDR writer this file never calls.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STBI_WRITE_NO_STDIO
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
#include "stb/stb_image_write.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// frame_cost.cpp: gtlDraw's count, the frame stamp on a record.
extern "C" std::atomic<uint32_t> snap_draw_serial;

namespace snap {
// overlay_hook.cpp: true while a course's code is loaded. The window
// library lives inside the same address range, so the two are never
// resident together.
extern std::atomic<bool> g_app_level_resident;
}

namespace snap {
namespace {

// The window library's variables (decomp src/window/847B60.c). Section 108
// loads at 0x80369F80 and the caller's relocations are 0x3CC90 and 0x3C6FC.
constexpr uint32_t WindowPhotoBuffer = 0x803A6C10u;   // u16* D_803A6C10_87A3C0
constexpr uint32_t WindowPhotoData   = 0x803A667Cu;   // PhotoData* D_803A667C_879E2C
constexpr uint32_t PhotoDataSize     = 0x3A0u;
constexpr uint32_t RdramSize         = 0x00800000u;

// A buffer wider than the screen or taller than it is not one this game
// renders, and a bound keeps a corrupt word from asking for a huge read.
constexpr int32_t MaxBufferSide = 1024;

// gLevelNames in the decomp's src/app_render/47330.c, as file-name tokens.
constexpr const char* CourseTokens[] = {
    "beach", "tunnel", "volcano", "river", "cave", "valley", "rainbow",
};
constexpr int32_t CourseCount = int32_t(sizeof(CourseTokens) / sizeof(CourseTokens[0]));

struct Target {
    uint32_t serial = 0;        // 0: nothing recorded yet
    uint32_t buffer = 0;        // KSEG0 address of the RGBA16 colour image
    int32_t  width = 0;         // the buffer's dimensions
    int32_t  height = 0;
    int32_t  x0 = 0;            // the rendered region, [x0,x1) by [y0,y1)
    int32_t  y0 = 0;
    int32_t  x1 = 0;
    int32_t  y1 = 0;
    int32_t  course = -1;       // 0..6, or -1 when not certain
    uint32_t drawSerial = 0;    // gtlDraw count when it was rendered
    uint32_t reading = 0;       // the reading clock when it was rendered
};

std::mutex s_targetLock;
Target s_target;

// Advanced once per controller reading (photo_export_on_reading). A record
// made during reading N is complete -- the RDP has finished and the write-back
// has landed -- before reading N+1 begins, because the render is a blocking
// draw inside the game's update.
std::atomic<uint32_t> s_readings{0};

// One export at a time: two callers computing the same second's name would
// otherwise both find it free.
std::mutex s_exportLock;

// The last record the diagnostic auto-export saved.
std::atomic<uint32_t> s_autoExported{0};

bool ram_span_ok(uint32_t address, uint32_t bytes) {
    if ((address >> 29) != 4u) {
        return false;
    }
    const uint32_t physical = address & 0x1FFFFFFFu;
    return (bytes <= RdramSize) && (physical <= RdramSize - bytes);
}

std::string path_text(const std::filesystem::path& path) {
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.c_str()), u8.size());
}

void say_not_saved(const std::string& why) {
    printf("[SNAP] photo not saved: %s\n", why.c_str());
    fflush(stdout);
}

// Local time as YYYYMMDD_HHMMSS; UTC if local time cannot be produced.
std::string timestamp_now() {
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    bool have = false;
#if defined(_WIN32)
    have = (localtime_s(&tm, &t) == 0);
    if (!have) {
        have = (gmtime_s(&tm, &t) == 0);
    }
#else
    have = (localtime_r(&t, &tm) != nullptr);
    if (!have) {
        have = (gmtime_r(&t, &tm) != nullptr);
    }
#endif
    if (!have) {
        return std::string();
    }
    char text[32];
    snprintf(text, sizeof(text), "%04d%02d%02d_%02d%02d%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return text;
}

// stb hands the finished PNG over in one call; collect it. An allocation
// failure is recorded rather than thrown: an exception unwinding through
// stb would leak the PNG it is holding in malloc'd memory.
struct PngSink {
    std::vector<unsigned char> bytes;
    bool failed = false;
};

void collect_png(void* context, void* data, int size) {
    auto* sink = static_cast<PngSink*>(context);
    if (sink->failed || (size <= 0)) {
        return;
    }
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    try {
        sink->bytes.insert(sink->bytes.end(), bytes, bytes + size);
    } catch (const std::bad_alloc&) {
        sink->failed = true;
        sink->bytes.clear();
    }
}

// Reads the rendered region out of RDRAM as packed RGB8.
std::vector<unsigned char> read_pixels(uint8_t* rdram, const Target& t) {
    const int32_t w = t.x1 - t.x0;
    const int32_t h = t.y1 - t.y0;
    std::vector<unsigned char> rgb(size_t(w) * size_t(h) * 3u);
    unsigned char* out = rgb.data();
    for (int32_t y = 0; y < h; y++) {
        const uint32_t row = t.buffer + (uint32_t(t.y0 + y) * uint32_t(t.width) + uint32_t(t.x0)) * 2u;
        for (int32_t x = 0; x < w; x++) {
            const uint32_t address = row + uint32_t(x) * 2u;
            const uint16_t pixel = MEM_HU(0, (gpr)(int32_t)address);
            const unsigned r = (pixel >> 11) & 31u;
            const unsigned g = (pixel >> 6) & 31u;
            const unsigned b = (pixel >> 1) & 31u;
            // Five bits to eight, the top bits repeated below: 31 -> 255, 0 -> 0.
            out[0] = static_cast<unsigned char>((r << 3) | (r >> 2));
            out[1] = static_cast<unsigned char>((g << 3) | (g >> 2));
            out[2] = static_cast<unsigned char>((b << 3) | (b >> 2));
            out += 3;
        }
    }
    return rgb;
}

// Everything after the checks: the read, the encode, the file. Returns
// false after printing why.
bool write_photo(uint8_t* rdram, const Target& t) {
    const int32_t w = t.x1 - t.x0;
    const int32_t h = t.y1 - t.y0;

    PngSink sink;
    try {
        const std::vector<unsigned char> rgb = read_pixels(rdram, t);
        if (!stbi_write_png_to_func(collect_png, &sink, w, h, 3, rgb.data(), w * 3)) {
            say_not_saved("the PNG encoder failed (out of memory)");
            return false;
        }
    } catch (const std::bad_alloc&) {
        say_not_saved("out of memory while reading the photo");
        return false;
    }
    if (sink.failed) {
        say_not_saved("out of memory while collecting the PNG");
        return false;
    }
    const std::vector<unsigned char>& png = sink.bytes;
    if (png.empty()) {
        say_not_saved("the PNG encoder produced no data");
        return false;
    }

    const std::filesystem::path dir = base_path("photos");
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        say_not_saved("cannot create " + path_text(dir) + " (" + ec.message() + ")");
        return false;
    }

    std::string stem = "snap_" + timestamp_now();
    if (stem == "snap_") {
        // No clock at all: the record's serial still makes the name unique
        // within the run, and the counter below covers the rest.
        stem += "render" + std::to_string(t.serial);
    }
    if (t.course >= 0 && t.course < CourseCount) {
        stem += "_";
        stem += CourseTokens[t.course];
    }

    std::filesystem::path path;
    bool named = false;
    for (int n = 0; n < 100; n++) {
        char suffix[8];
        snprintf(suffix, sizeof(suffix), "_%02d", n);
        path = dir / (stem + suffix + ".png");
        if (!std::filesystem::exists(path, ec) && !ec) {
            named = true;
            break;
        }
    }
    if (!named) {
        say_not_saved("a hundred files already carry the name " + path_text(dir / (stem + "_NN.png")));
        return false;
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        say_not_saved("cannot create " + path_text(path));
        return false;
    }
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    out.flush();
    const bool written = out.good();
    out.close();
    if (!written || !out) {
        // A short file is worse than none: it opens as a broken image.
        std::filesystem::remove(path, ec);
        say_not_saved("writing " + path_text(path) + " failed; the file was removed");
        return false;
    }

    printf("[SNAP] photo saved: %s (%dx%d, %s, rendered at draw %u)\n",
           path_text(path).c_str(), w, h,
           (t.course >= 0 && t.course < CourseCount) ? CourseTokens[t.course] : "course unknown",
           t.drawSerial);
    fflush(stdout);
    return true;
}

} // namespace

void export_photo(uint8_t* rdram) {
    std::lock_guard<std::mutex> exporting(s_exportLock);

    if (rdram == nullptr) {
        say_not_saved("the game has not started");
        return;
    }

    Target t;
    {
        std::lock_guard<std::mutex> lock(s_targetLock);
        t = s_target;
    }
    if (t.serial == 0) {
        say_not_saved("no photo has been rendered yet");
        return;
    }
    if (!settings().render_to_ram) {
        say_not_saved("render-to-RAM is off (F6), so the game's photo buffer is never written back and cannot be read");
        return;
    }
    if (g_app_level_resident.load(std::memory_order_relaxed)) {
        say_not_saved("no photo is on screen (a course is running)");
        return;
    }
    // The library that owns the buffer must still be the code at that
    // address: once another overlay loads over it, the buffer's memory
    // belongs to something else and the record describes nothing.
    if (static_cast<uint32_t>(MEM_W(0, (gpr)(int32_t)WindowPhotoBuffer)) != t.buffer) {
        say_not_saved("no photo is on screen (the last one rendered is no longer in memory)");
        return;
    }
    if (s_readings.load(std::memory_order_acquire) == t.reading) {
        say_not_saved("the photo was rendered this very tick and may still be on its way into memory; press again");
        return;
    }
    // Checked when recorded; checked again because a record outlives the
    // moment it was taken.
    const uint32_t bytes = uint32_t(t.width) * uint32_t(t.height) * 2u;
    if (!ram_span_ok(t.buffer, bytes) || (t.x0 < 0) || (t.y0 < 0) || (t.x1 > t.width) ||
        (t.y1 > t.height) || (t.x0 >= t.x1) || (t.y0 >= t.y1)) {
        say_not_saved("the recorded photo buffer does not fit in memory");
        return;
    }

    write_photo(rdram, t);
}

void photo_export_on_reading(uint8_t* rdram) {
    const uint32_t reading = s_readings.fetch_add(1, std::memory_order_acq_rel) + 1;

    // Presence turns it on, as with SNAP_STATS; "0" is the one value that
    // does not, so a shell that exports it once can switch it off again.
    static const bool autoExport = [] {
        if (!snapdiag::statsEnabled()) {
            return false;
        }
        const char* env = std::getenv("SNAP_PHOTO_AUTOEXPORT");
        const bool on = (env != nullptr) && (env[0] != '0');
        if (on) {
            printf("[SNAP-PHOTO] auto-export armed: every photo the game renders is saved to photos/ two readings after it is rendered\n");
            fflush(stdout);
        }
        return on;
    }();
    if (!autoExport) {
        return;
    }

    Target t;
    {
        std::lock_guard<std::mutex> lock(s_targetLock);
        t = s_target;
    }
    if ((t.serial == 0) || (t.serial == s_autoExported.load(std::memory_order_relaxed))) {
        return;
    }
    if (reading - t.reading < 2u) {
        return;
    }
    s_autoExported.store(t.serial, std::memory_order_relaxed);
    printf("[SNAP-PHOTO] auto-export: render #%u (buffer %08X) at reading %u\n", t.serial, t.buffer, reading);
    fflush(stdout);
    export_photo(rdram);
}

} // namespace snap

// renInitCameraEx(Gfx** gfxPtr, OMCamera* cam, s32 mode, u16* buffer,
//                 s32 width, s32 height, u16* zbuffer)
// a0..a3 in r4..r7; width, height and zbuffer on the caller's stack at
// sp+0x10, +0x14, +0x18 (MIPS o32). The camera's Vp sits at cam+8: vscale[0]
// at +8, vscale[1] at +0xA, vtrans[0] at +0x10, vtrans[1] at +0x12, which is
// what the recompiled function loads.
extern "C" void snap_photo_note_render_target(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t cam = static_cast<uint32_t>(ctx->r5);
    const uint32_t buffer = static_cast<uint32_t>(ctx->r7);
    const int32_t width = MEM_W(0x10, ctx->r29);
    const int32_t height = MEM_W(0x14, ctx->r29);

    const bool stats = snapdiag::statsEnabled();
    if ((width <= 0) || (height <= 0) || (width > snap::MaxBufferSide) || (height > snap::MaxBufferSide) ||
        !snap::ram_span_ok(buffer, uint32_t(width) * uint32_t(height) * 2u) || !snap::ram_span_ok(cam, 0x14u)) {
        if (stats) {
            printf("[SNAP-PHOTO] render target ignored: buffer %08X %dx%d, camera %08X\n", buffer, width, height, cam);
            fflush(stdout);
        }
        return;
    }

    const gpr camReg = (gpr)(int32_t)cam;
    const int32_t vscaleX = MEM_H(0x8, camReg);
    const int32_t vscaleY = MEM_H(0xA, camReg);
    const int32_t vtransX = MEM_H(0x10, camReg);
    const int32_t vtransY = MEM_H(0x12, camReg);
    // Each quotient truncates toward zero on its own, as the game's s32
    // division does, before the two are combined.
    int32_t x0 = vtransX / 4 - vscaleX / 4;
    int32_t y0 = vtransY / 4 - vscaleY / 4;
    int32_t x1 = vtransX / 4 + vscaleX / 4;
    int32_t y1 = vtransY / 4 + vscaleY / 4;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > width) x1 = width;
    if (y1 > height) y1 = height;
    if ((x0 >= x1) || (y0 >= y1)) {
        if (stats) {
            printf("[SNAP-PHOTO] render target ignored: empty viewport [%d,%d)-[%d,%d) on buffer %08X\n",
                   x0, x1, y0, y1, buffer);
            fflush(stdout);
        }
        return;
    }

    // The course, only when the window library vouches for this buffer.
    int32_t course = -1;
    if (static_cast<uint32_t>(MEM_W(0, (gpr)(int32_t)snap::WindowPhotoBuffer)) == buffer) {
        const uint32_t photo = static_cast<uint32_t>(MEM_W(0, (gpr)(int32_t)snap::WindowPhotoData));
        if (((photo & 3u) == 0u) && snap::ram_span_ok(photo, snap::PhotoDataSize)) {
            const int32_t levelID = MEM_W(0, (gpr)(int32_t)photo) >> 25;
            if ((levelID >= 0) && (levelID < snap::CourseCount)) {
                course = levelID;
            }
        }
    }

    snap::Target t;
    t.buffer = buffer;
    t.width = width;
    t.height = height;
    t.x0 = x0;
    t.y0 = y0;
    t.x1 = x1;
    t.y1 = y1;
    t.course = course;
    t.drawSerial = snap_draw_serial.load(std::memory_order_relaxed);
    t.reading = snap::s_readings.load(std::memory_order_acquire);
    {
        std::lock_guard<std::mutex> lock(snap::s_targetLock);
        t.serial = snap::s_target.serial + 1;
        snap::s_target = t;
    }

    if (stats) {
        printf("[SNAP-PHOTO] render #%u: buffer %08X %dx%d, photo [%d,%d)x[%d,%d), course %s, draw %u, reading %u\n",
               t.serial, buffer, width, height, x0, x1, y0, y1,
               (course >= 0) ? snap::CourseTokens[course] : "unknown", t.drawSerial, t.reading);
        fflush(stdout);
    }
}
