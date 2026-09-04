/**
 * @file snap_station.cpp
 * @brief The Pokemon Snap Station, emulated on controller port 4.
 *
 * The device side of what snap_station.h describes: the two Controller Pak
 * addresses the game uses, the message bytes, the busy hold, the capture of
 * each displayed slot, the sheet, and the two resets, which are relaunches
 * of this executable. The game's own code (decomp src/sys/cont.c,
 * src/gallery/9FAC10.c, src/more_funcs/5BF20.c, src/app_render/46270.c,
 * src/AA18E0.c) drives all of it; nothing here decides what the game shows.
 */
#include "snap_station.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#endif

#include "ultramodern/ultramodern.hpp"
#include "ultramodern/renderer_context.hpp"
// Its own static copy of the PNG writer, as photo_export.cpp keeps one: the
// header is compiled into the file that uses it.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb/stb_image_write.h"

#include "paths.h"
#include "settings.h"

// osViGetCurrentFramebuffer (ultramodern/src/events.cpp, declared in
// ultra64.h) is the buffer the emulated video interface is scanning out,
// which is what the kiosk's printer received through the console's video
// output.

// The renderer's presented-frame capture (lib/rt64/src/hle/rt64_present_queue.cpp):
// _pending counts presents still to photograph; _station is the gate this
// file holds open while a capture is armed, so the capture needs neither the
// diagnostic environment nor a schedule.
extern "C" std::atomic<int32_t> snap_frame_dump_pending;
extern "C" std::atomic<int32_t> snap_frame_dump_station;

namespace snap {
namespace {

// ---------------------------------------------------------------------------
// The protocol, as the game speaks it (decomp src/sys/cont.c)
// ---------------------------------------------------------------------------
constexpr int32_t StationChannel = 3;             // port 4
constexpr uint32_t DetectBlock  = 0x8000 / 32;    // CONT_BLOCK_DETECT
constexpr uint32_t MessageBlock = 0xC000 / 32;    // contPrinterReadWrite
constexpr uint32_t BlockSize    = 32;
constexpr uint8_t PakCheckByte  = 0xFE;           // OS_PFS_CHECK_ID, which the station must not echo
constexpr uint8_t PrinterId     = 0x85;           // echoed: CONT_DEV_TYPE_PRINTER
constexpr uint8_t BusyByte      = 0x08;           // "ask again"

// Message bytes, the last byte of each 32-byte write to 0xC000.
constexpr uint8_t MsgSaving       = 0xCC;  // func_800BF244_5C0E4, before the save
constexpr uint8_t MsgSaved        = 0x33;  // after it
constexpr uint8_t MsgResetPlease  = 0x5A;  // the Gallery: reset the console now
constexpr uint8_t MsgDisplayStart = 0x01;  // func_801DC9D0_AA1A10, before slot 0
constexpr uint8_t MsgPhotoShown   = 0x02;  // one per slot, the picture is on screen
constexpr uint8_t MsgDisplayDone  = 0x04;  // after slot 15 (and again on START)
constexpr uint8_t MsgTooFewPhotos = 0x10;  // func_801DC8A0_AA18E0(2): fewer than four photos

constexpr int SlotCount = 16;        // func_8009B40C runs slots 0..15, then 16 ends it
constexpr int SheetColumns = 4;      // ROM 0xAAA508: 4x4, each photo a 2x2 block
constexpr int PresenceDelaySeconds = 5;
constexpr const char* MarkerName = "snapstation.job";
constexpr const char* OutputDirName = "stickers";
constexpr const char* SaveFileRel = "saves/pokemonsnap.bin";

enum class Capture { Idle, Armed };

struct Frame {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgb;      // the VI framebuffer, RGB8
    std::filesystem::path presented;     // the renderer's frame, when it arrived
};

struct Station {
    std::atomic<bool> enabled{false};
    std::atomic<bool> jobPending{false};
    std::atomic<bool> everPresent{false};
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    std::mutex mutex;
    uint8_t detectByte = 0;
    uint8_t lastMessage = 0;
    bool busy = false;
    bool resetRequested = false;
    Capture capture = Capture::Idle;
    std::chrono::steady_clock::time_point armedAt;
    std::filesystem::file_time_type armedAtFile{};
    std::set<std::filesystem::path> dumpsBefore;
    std::filesystem::path sheetDir;
    std::vector<Frame> frames;
    std::filesystem::file_time_type saveTimeAtCC{};
    bool haveSaveTimeAtCC = false;
    // The pace of a slot after its capture, driven from the game's own
    // status polls while it waits on the busy byte: 1 = the photo stays up
    // while the printer "captures", 2 = the printer's grid is up; then the
    // game is released and a watcher keeps the grid until the next photo is
    // on screen (see slot_release below).
    int slotPhase = 0;
    std::chrono::steady_clock::time_point phaseUntil;
    uint8_t* rdram = nullptr;
};

Station& st() {
    static Station s;
    return s;
}

void say(const char* fmt, ...) {
    char line[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    printf("[SNAP-STATION] %s\n", line);
    fflush(stdout);
}

std::string path_text(const std::filesystem::path& p) {
    return std::string(reinterpret_cast<const char*>(p.u8string().c_str()));
}

std::string timestamp_now() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[32];
    std::strftime(text, sizeof(text), "%Y-%m-%d_%H%M%S", &local);
    return text;
}

std::filesystem::path marker_path() { return base_path(MarkerName); }
std::filesystem::path dumps_dir() {
    std::error_code ec;
    return std::filesystem::current_path(ec) / "snap_frame_dumps";
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------
struct PngSink {
    std::vector<unsigned char> bytes;
    bool failed = false;
};

void collect_png(void* context, void* data, int size) {
    PngSink* sink = static_cast<PngSink*>(context);
    if (sink->failed || size <= 0) {
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

bool write_png(const std::filesystem::path& path, int w, int h, const unsigned char* rgb) {
    PngSink sink;
    if (!stbi_write_png_to_func(collect_png, &sink, w, h, 3, rgb, w * 3) || sink.failed || sink.bytes.empty()) {
        say("could not encode %s", path_text(path).c_str());
        return false;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        say("could not open %s for writing", path_text(path).c_str());
        return false;
    }
    out.write(reinterpret_cast<const char*>(sink.bytes.data()), std::streamsize(sink.bytes.size()));
    return bool(out);
}

// A 24-bit bottom-up BMP, the form the present capture writes
// (rt64_snap_diag.h writeBMP24). Returns false for anything else.
bool read_bmp24(const std::filesystem::path& path, int& w, int& h, std::vector<unsigned char>& rgb) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::vector<unsigned char> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (d.size() < 54 || d[0] != 'B' || d[1] != 'M') {
        return false;
    }
    auto u32 = [&](size_t at) { return uint32_t(d[at]) | (uint32_t(d[at + 1]) << 8) | (uint32_t(d[at + 2]) << 16) | (uint32_t(d[at + 3]) << 24); };
    auto u16 = [&](size_t at) { return uint32_t(d[at]) | (uint32_t(d[at + 1]) << 8); };
    const uint32_t offset = u32(10);
    const int32_t width = int32_t(u32(18));
    const int32_t height = int32_t(u32(22));
    const uint32_t bpp = u16(28);
    if (bpp != 24 || width <= 0 || height == 0) {
        return false;
    }
    const int32_t rows = height < 0 ? -height : height;
    const size_t stride = (size_t(width) * 3u + 3u) & ~size_t(3);
    if (offset + stride * size_t(rows) > d.size()) {
        return false;
    }
    rgb.assign(size_t(width) * size_t(rows) * 3u, 0);
    for (int32_t y = 0; y < rows; y++) {
        const int32_t srcRow = (height > 0) ? (rows - 1 - y) : y;
        const unsigned char* src = d.data() + offset + stride * size_t(srcRow);
        unsigned char* dst = rgb.data() + size_t(y) * size_t(width) * 3u;
        for (int32_t x = 0; x < width; x++) {
            dst[x * 3 + 0] = src[x * 3 + 2];
            dst[x * 3 + 1] = src[x * 3 + 1];
            dst[x * 3 + 2] = src[x * 3 + 0];
        }
    }
    w = width;
    h = rows;
    return true;
}

// Lays frames of one size out on a grid, SheetColumns wide, in slot order.
bool compose_sheet(const std::filesystem::path& path, const std::vector<const Frame*>& frames,
                   const std::vector<const std::vector<unsigned char>*>& pixels, int w, int h) {
    const int count = int(pixels.size());
    const int rows = (count + SheetColumns - 1) / SheetColumns;
    const size_t sheetW = size_t(w) * SheetColumns;
    const size_t sheetH = size_t(h) * rows;
    std::vector<unsigned char> sheet;
    try {
        sheet.assign(sheetW * sheetH * 3u, 0);
    } catch (const std::bad_alloc&) {
        say("not enough memory for a %zux%zu sheet", sheetW, sheetH);
        return false;
    }
    for (int i = 0; i < count; i++) {
        const int col = i % SheetColumns;
        const int row = i / SheetColumns;
        const std::vector<unsigned char>& src = *pixels[size_t(i)];
        for (int y = 0; y < h; y++) {
            unsigned char* dst = sheet.data() + ((size_t(row) * h + y) * sheetW + size_t(col) * w) * 3u;
            std::memcpy(dst, src.data() + size_t(y) * size_t(w) * 3u, size_t(w) * 3u);
        }
    }
    (void)frames;
    return write_png(path, int(sheetW), int(sheetH), sheet.data());
}

// ---------------------------------------------------------------------------
// The VI framebuffer, as the printer saw it
// ---------------------------------------------------------------------------
bool read_vi_frame(uint8_t* rdram, Frame& f) {
    ultramodern::renderer::ViRegs* regs = ultramodern::renderer::get_vi_regs();
    const uint32_t origin = uint32_t(osViGetCurrentFramebuffer());
    uint32_t width = (regs != nullptr) ? regs->VI_WIDTH_REG : 0u;
    if (width == 0 || width > 1024) {
        width = 320;
    }
    // The display mode's ScreenSettings (ROM 0xAAA550) are 640x480; the
    // game's ordinary screens are 320x240. The VI carries no height register
    // of its own, so it follows the width.
    const uint32_t height = (width >= 640) ? 480u : 240u;
    const uint32_t bytes = width * height * 2u;
    if (origin < 0x80000000u || (origin - 0x80000000u) + bytes > 0x00800000u) {
        say("VI framebuffer at 0x%08X is outside RDRAM; slot not captured", origin);
        return false;
    }
    try {
        f.rgb.assign(size_t(width) * size_t(height) * 3u, 0);
    } catch (const std::bad_alloc&) {
        return false;
    }
    unsigned char* out = f.rgb.data();
    for (uint32_t y = 0; y < height; y++) {
        const uint32_t row = origin + y * width * 2u;
        for (uint32_t x = 0; x < width; x++) {
            const uint32_t address = row + x * 2u;
            const uint16_t pixel = MEM_HU(0, (gpr)(int32_t)address);
            const unsigned r = (pixel >> 11) & 31u;
            const unsigned g = (pixel >> 6) & 31u;
            const unsigned b = (pixel >> 1) & 31u;
            out[0] = static_cast<unsigned char>((r << 3) | (r >> 2));
            out[1] = static_cast<unsigned char>((g << 3) | (g >> 2));
            out[2] = static_cast<unsigned char>((b << 3) | (b >> 2));
            out += 3;
        }
    }
    f.width = int(width);
    f.height = int(height);
    return true;
}

// ---------------------------------------------------------------------------
// The resets: a relaunch of this executable
// ---------------------------------------------------------------------------
// mode, pid, time, and an optional fourth line: the sheet folder a "restart"
// carries so the normal boot can open it.
void write_marker(const char* mode, const std::string& extra) {
    std::ofstream out(marker_path(), std::ios::trunc);
    if (!out) {
        say("could not write %s", path_text(marker_path()).c_str());
        return;
    }
#if defined(_WIN32)
    const unsigned long pid = GetCurrentProcessId();
#else
    const unsigned long pid = 0;
#endif
    out << mode << "\n" << pid << "\n" << std::time(nullptr) << "\n" << extra << "\n";
}

void remove_marker() {
    std::error_code ec;
    std::filesystem::remove(marker_path(), ec);
}

// Starts a fresh copy of this program in the executable's directory and asks
// this one to quit. The new process reads the marker and waits for this
// process to be gone before it touches the save or the shader cache.
void relaunch_self(const char* why) {
#if defined(_WIN32)
    wchar_t exe[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        say("could not find this executable's path; not relaunching (%s)", why);
        return;
    }
    std::wstring command = L"\"" + std::wstring(exe) + L"\"";
    // The new process is a plain launch: an input replay or a capture
    // schedule belongs to the run that is ending, not to the boot the kiosk
    // is resetting into.
    for (const wchar_t* name : { L"SNAP_REPLAY", L"SNAP_RECORD", L"SNAP_PCAP_AT", L"SNAP_PCAP_EVERY",
                                 L"SNAP_PCAP_START", L"SNAP_PCAP_BURST", L"SNAP_PCAP_FX", L"SNAP_PHOTO_AUTOEXPORT" }) {
        SetEnvironmentVariableW(name, nullptr);
    }
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const std::wstring cwd = base_dir().wstring();
    if (!CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, 0, nullptr, cwd.c_str(), &si, &pi)) {
        say("CreateProcess failed with error %lu; not relaunching (%s)", GetLastError(), why);
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    say("relaunched for %s (pid %lu); this instance is quitting", why, pi.dwProcessId);
    // The new process writes snap64.log beside the executable and keeps
    // this one's as snap64.prev.log; it can only do that once this process
    // lets go of the file. Released now rather than at exit, which comes a
    // second or more later and left the child unable to open its log.
    fflush(stdout);
    fflush(stderr);
    FILE* sink = nullptr;
    freopen_s(&sink, "NUL", "w", stdout);
    freopen_s(&sink, "NUL", "w", stderr);
    ultramodern::quit();
#else
    (void)why;
#endif
}

// The runtime writes the save on its own thread, coalescing the game's writes
// for a few milliseconds (librecomp pi.cpp). Between 0x33 and 0x5A the file
// is normally on disk already; this waits until it has changed since 0xCC and
// has been quiet for a moment, and says so when it never changes.
void wait_for_save_to_settle() {
    const std::filesystem::path save = base_path(SaveFileRel);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    std::error_code ec;
    bool changed = false;
    std::filesystem::file_time_type last{};
    while (std::chrono::steady_clock::now() < deadline) {
        const auto now = std::filesystem::last_write_time(save, ec);
        if (!ec) {
            if (!st().haveSaveTimeAtCC || now != st().saveTimeAtCC) {
                changed = true;
            }
            if (changed) {
                if (now == last) {
                    // Quiet for a whole poll: settled.
                    break;
                }
                last = now;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    if (!changed) {
        say("the save file did not change after the print selection was written; continuing anyway");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

void schedule_relaunch(const char* mode, const char* why, int delayMs, bool settleSave,
                       const std::string& extra = std::string()) {
    std::thread([mode, why, delayMs, settleSave, extra]() {
        if (settleSave) {
            wait_for_save_to_settle();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        write_marker(mode, extra);
        relaunch_self(why);
    }).detach();
}

#if defined(_WIN32)
// The kiosk handed the player the sheet; the port opens its folder as the
// normal boot after a print gets under way.
void open_folder(const std::string& utf8) {
    const std::filesystem::path p(std::u8string(reinterpret_cast<const char8_t*>(utf8.c_str())));
    std::error_code ec;
    if (!std::filesystem::is_directory(p, ec)) {
        return;
    }
    // ShellExecute may hand the open to a shell extension, which wants COM
    // initialised on the calling thread.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ShellExecuteW(nullptr, L"open", p.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    say("the printed sheet is in %s", utf8.c_str());
}
#endif

} // namespace
} // namespace snap

// ---------------------------------------------------------------------------
// The printer's display
// ---------------------------------------------------------------------------
// The kiosk's video printer sat between the console and the monitor and, while
// it worked, showed its own picture: after each capture, the sixteen-picture
// grid it had collected so far (the empty places light grey, thin white lines
// between them), and after the last, that grid under "PRINTING... PLEASE
// WAIT" with three marks over the upper right that became stars one by one,
// each popping up and blinking a few times before it held, as each of the
// printer's three passes finished. Footage of a working station, 3 September
// 2026; the game itself draws none of it (decomp src/AA18E0.c shows each photo
// and waits on the busy byte). The pictures are composed here from the
// captured frames and handed to the renderer (lib/rt64 .. rt64_snap_overlay.h),
// which presents them in place of the game's frame. The lettering is a plain
// on-screen-display face drawn here, as the printer's was its own; the pass
// times are an estimate, the footage having no clock.
extern "C" void snap_overlay_show(const uint8_t* rgba, uint32_t width, uint32_t height);
extern "C" void snap_overlay_hide();

namespace snap {
namespace {

constexpr int OsdW = 640;
constexpr int OsdH = 480;
// The printer tiled its sixteen captures edge to edge: the white hem the
// game draws around every photo is what reads as the lines between them.
constexpr int OsdCellW = OsdW / 4;   // 160
constexpr int OsdCellH = OsdH / 4;   // 120
constexpr int PhotoHoldMs = 1000;        // the photo stays up while the printer "captures"
constexpr int GridHoldMs = 1200;         // the printer's grid after each capture
constexpr int NewPhotoWaitMs = 2000;     // the grid stays until the next photo is on screen, at most this
constexpr int PrintDashesMs = 2000;      // "PRINTING..." with three dashes
constexpr int PrintPassMs = 8000;        // one printer pass, dash to star
constexpr int StarBlinkMs = 250;
constexpr int StarBlinks = 3;
constexpr int PrintFinalHoldMs = 2500;

// The printer's on-screen-display face, measured against the sticker grid
// in two photographs of the real screen (2026-09-03; the grid's cells are
// 160 by 120, the one known size in them): capitals thirty-six pixels tall
// and sixteen wide, stems four wide and bars three tall, on a twenty-three
// pixel pitch, so seven pixels between letters. Five by twelve dots of
// three by three, each dot widened a pixel to the right (OsdFontBoldX).
// Only the letters its two lines use; the three dots after PRINTING are
// not on the letter pitch and are drawn on their own (osd_printing).
constexpr int OsdFontRows = 12;
constexpr int OsdFontScaleX = 3;
constexpr int OsdFontScaleY = 3;
constexpr int OsdFontBoldX = 1;
constexpr int OsdFontAdvance = 23;
struct OsdGlyph {
    char ch;
    const char* rows[OsdFontRows];
};
constexpr OsdGlyph kOsdFont[] = {
    { 'P', { "####.", "#...#", "#...#", "#...#", "####.", "#....", "#....", "#....", "#....", "#....", "#....", "#...." } },
    { 'R', { "####.", "#...#", "#...#", "#...#", "####.", "#.#..", "#..#.", "#..#.", "#...#", "#...#", "#...#", "#...#" } },
    { 'I', { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#..", "..#..", "..#..", "..#..", "..#..", "#####" } },
    { 'N', { "#...#", "##..#", "##..#", "#.#.#", "#.#.#", "#.#.#", "#..##", "#..##", "#...#", "#...#", "#...#", "#...#" } },
    { 'T', { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.." } },
    { 'G', { ".###.", "#...#", "#....", "#....", "#....", "#....", "#..##", "#...#", "#...#", "#...#", "#...#", ".####" } },
    { 'L', { "#....", "#....", "#....", "#....", "#....", "#....", "#....", "#....", "#....", "#....", "#....", "#####" } },
    { 'E', { "#####", "#....", "#....", "#....", "#....", "####.", "#....", "#....", "#....", "#....", "#....", "#####" } },
    { 'A', { ".###.", "#...#", "#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#", "#...#", "#...#", "#...#" } },
    { 'S', { ".####", "#....", "#....", "#....", ".#...", "..#..", "...#.", "....#", "....#", "....#", "#...#", "####." } },
    { 'W', { "#...#", "#...#", "#...#", "#...#", "#...#", "#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#" } },
    { '.', { ".....", ".....", ".....", ".....", ".....", ".....", ".....", ".....", ".....", ".....", ".....", "..#.." } },
};

void osd_put(std::vector<uint8_t>& img, int x, int y, uint8_t r, uint8_t g, uint8_t b);

// A five-pointed star, filled, centred on (cx, cy): outer radius r, inner
// radius 0.42 r, a point straight up. Even-odd against the ten edges.
bool osd_in_star(float px, float py, float cx, float cy, float r) {
    float vx[10], vy[10];
    for (int i = 0; i < 10; i++) {
        const float ang = -3.14159265f / 2.0f + float(i) * (3.14159265f / 5.0f);
        const float rad = (i % 2 == 0) ? r : r * 0.5f;
        vx[i] = cx + rad * std::cos(ang);
        vy[i] = cy + rad * std::sin(ang);
    }
    bool inside = false;
    for (int i = 0, j = 9; i < 10; j = i++) {
        if (((vy[i] > py) != (vy[j] > py)) &&
            (px < (vx[j] - vx[i]) * (py - vy[i]) / (vy[j] - vy[i]) + vx[i])) {
            inside = !inside;
        }
    }
    return inside;
}

void osd_star(std::vector<uint8_t>& img, int cx, int cy, float r) {
    const int reach = int(r) + 4;
    for (int y = cy - reach; y <= cy + reach; y++) {
        for (int x = cx - reach; x <= cx + reach; x++) {
            if (osd_in_star(float(x) + 0.5f, float(y) + 0.5f, float(cx), float(cy), r + 1.5f)) {
                osd_put(img, x, y, 0, 0, 0);
            }
        }
    }
    for (int y = cy - reach; y <= cy + reach; y++) {
        for (int x = cx - reach; x <= cx + reach; x++) {
            if (osd_in_star(float(x) + 0.5f, float(y) + 0.5f, float(cx), float(cy), r)) {
                osd_put(img, x, y, 255, 255, 255);
            }
        }
    }
}

void osd_put(std::vector<uint8_t>& img, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if ((x < 0) || (x >= OsdW) || (y < 0) || (y >= OsdH)) {
        return;
    }
    uint8_t* p = img.data() + (size_t(y) * OsdW + size_t(x)) * 4u;
    p[0] = r; p[1] = g; p[2] = b; p[3] = 255;
}

// A glyph at a pixel scale: white, with a black ring one glyph pixel wide, the
// way an on-screen display keeps its text legible over any picture.
// A glyph's dots are scaleX by scaleY pixels, each widened by boldX to the
// right (a stem is then scaleX + boldX wide, a bar scaleY tall); the black
// ring around the white is one dot wide, as the video's character
// generator drew it.
void osd_glyph(std::vector<uint8_t>& img, const char* const* rows, int nrows, int x, int y, int scaleX, int scaleY, int boldX) {
    const int ncols = int(std::strlen(rows[0]));
    auto ink = [&](int r, int c) {
        return (r >= 0) && (r < nrows) && (c >= 0) && (c < ncols) && (rows[r][c] == '#');
    };
    auto dot = [&](int r, int c, uint8_t v) {
        for (int sy = 0; sy < scaleY; sy++) {
            for (int sx = 0; sx < scaleX + boldX; sx++) {
                osd_put(img, x + c * scaleX + sx, y + r * scaleY + sy, v, v, v);
            }
        }
    };
    for (int r = -1; r <= nrows; r++) {
        for (int c = -1; c <= ncols; c++) {
            if (ink(r, c)) {
                continue;
            }
            bool edge = false;
            for (int dr = -1; (dr <= 1) && !edge; dr++) {
                for (int dc = -1; (dc <= 1) && !edge; dc++) {
                    edge = ink(r + dr, c + dc);
                }
            }
            if (edge) {
                dot(r, c, 0);
            }
        }
    }
    for (int r = 0; r < nrows; r++) {
        for (int c = 0; c < ncols; c++) {
            if (ink(r, c)) {
                dot(r, c, 255);
            }
        }
    }
}

void osd_text(std::vector<uint8_t>& img, const char* text, int x, int y) {
    for (const char* c = text; *c != 0; c++) {
        if (*c != ' ') {
            for (const OsdGlyph& g : kOsdFont) {
                if (g.ch == *c) {
                    osd_glyph(img, g.rows, OsdFontRows, x, y, OsdFontScaleX, OsdFontScaleY, OsdFontBoldX);
                    break;
                }
            }
        }
        x += OsdFontAdvance;
    }
}

// A thin white bar with a one-pixel black edge: the printer's mark for a
// pass still to come.
void osd_bar(std::vector<uint8_t>& img, int cx, int cy, int w, int h) {
    for (int y = cy - h / 2 - 1; y <= cy + h / 2 + 1; y++) {
        for (int x = cx - w / 2 - 1; x <= cx + w / 2 + 1; x++) {
            osd_put(img, x, y, 0, 0, 0);
        }
    }
    for (int y = cy - h / 2; y <= cy + h / 2; y++) {
        for (int x = cx - w / 2; x <= cx + w / 2; x++) {
            osd_put(img, x, y, 255, 255, 255);
        }
    }
}

// The three marks over the upper-right photos: a dash for a pass still to
// come, a star for one done; a blinking star is a star or nothing.
enum class Mark { Dash, Star, Blank };
void osd_marks(std::vector<uint8_t>& img, const Mark marks[3]) {
    // Over the lower part of the top row's third and fourth photos, centred
    // 445, 498 and 551 pixels in and 89 down (27 above the middle of the
    // hem between the first two rows), fifty-three apart: the stars
    // thirty-two across, the dashes twenty-four by six. Measured against
    // the sticker grid in two photographs of the real screen, the one thing
    // in them with known dimensions.
    for (int i = 0; i < 3; i++) {
        const int cx = 445 + i * 53;
        const int cy = 89;
        if (marks[i] == Mark::Star) {
            osd_star(img, cx, cy, 16.0f);
        }
        else if (marks[i] == Mark::Dash) {
            osd_bar(img, cx, cy, 23, 5);
        }
    }
}

// The grid the printer had collected: cells in the order the game showed
// them, each a box-filtered copy of its captured frame; light grey where
// nothing has been captured yet, white lines between.
std::vector<uint8_t> osd_grid(const std::vector<Frame>& frames) {
    // Empty places a near-white grey, as the footage's first frame shows
    // them before the second capture arrives.
    std::vector<uint8_t> img(size_t(OsdW) * OsdH * 4u, 255);
    for (int y = 0; y < OsdH; y++) {
        for (int x = 0; x < OsdW; x++) {
            osd_put(img, x, y, 240, 240, 240);
        }
    }
    for (size_t k = 0; (k < frames.size()) && (k < 16); k++) {
        const Frame& f = frames[k];
        if ((f.width <= 0) || (f.height <= 0) || (f.rgb.size() < size_t(f.width) * f.height * 3u)) {
            continue;
        }
        const int col = int(k % 4);
        const int row = int(k / 4);
        const int x0 = col * OsdCellW;
        const int y0 = row * OsdCellH;
        for (int y = 0; y < OsdCellH; y++) {
            const int sy0 = (y * f.height) / OsdCellH;
            const int sy1 = std::max(sy0 + 1, ((y + 1) * f.height) / OsdCellH);
            for (int x = 0; x < OsdCellW; x++) {
                const int sx0 = (x * f.width) / OsdCellW;
                const int sx1 = std::max(sx0 + 1, ((x + 1) * f.width) / OsdCellW);
                uint32_t r = 0, g = 0, b = 0, n = 0;
                for (int sy = sy0; sy < sy1; sy++) {
                    for (int sx = sx0; sx < sx1; sx++) {
                        const uint8_t* p = f.rgb.data() + (size_t(sy) * f.width + size_t(sx)) * 3u;
                        r += p[0]; g += p[1]; b += p[2]; n++;
                    }
                }
                osd_put(img, x0 + x, y0 + y, uint8_t(r / n), uint8_t(g / n), uint8_t(b / n));
            }
        }
    }
    return img;
}

std::vector<uint8_t> osd_printing(const std::vector<uint8_t>& grid, const Mark marks[3]) {
    std::vector<uint8_t> img = grid;
    // Where the photographs have them against the grid. The white line
    // between two rows of photos is the upper photo's own bottom hem, the
    // last 34 of its 480 lines, so in the grid it runs from 231 to 240
    // with its middle at 236; the first line of text has its top 27
    // pixels above that middle, so the hem crosses the letters three
    // quarters of the way down; the second line's top is seventy-four
    // lower; both start fourteen pixels into the second column. The three
    // dots after PRINTING are not on the letter pitch: seven-pixel squares
    // twenty-five apart, centred on the hem.
    osd_text(img, "PRINTING", 174, 209);
    for (int i = 0; i < 3; i++) {
        osd_bar(img, 387 + i * 25, 235, 6, 6);
    }
    osd_text(img, "PLEASE WAIT", 174, 283);
    osd_marks(img, marks);
    return img;
}

// The printing screen, from the first dash to the last held star. Blocks its
// caller for the whole print, so it runs on the finish thread.
void run_printing_display(const std::vector<uint8_t>& grid, const std::filesystem::path& sheetDir) {
    using namespace std::chrono;
    auto show = [&](const Mark marks[3]) {
        const std::vector<uint8_t> img = osd_printing(grid, marks);
        snap_overlay_show(img.data(), OsdW, OsdH);
    };
    Mark marks[3] = { Mark::Dash, Mark::Dash, Mark::Dash };
    show(marks);
    std::this_thread::sleep_for(milliseconds(PrintDashesMs));
    for (int pass = 0; pass < 3; pass++) {
        std::this_thread::sleep_for(milliseconds(PrintPassMs - 2 * StarBlinks * StarBlinkMs));
        for (int blink = 0; blink < StarBlinks; blink++) {
            marks[pass] = Mark::Star;
            show(marks);
            std::this_thread::sleep_for(milliseconds(StarBlinkMs));
            marks[pass] = Mark::Blank;
            show(marks);
            std::this_thread::sleep_for(milliseconds(StarBlinkMs));
        }
        marks[pass] = Mark::Star;
        show(marks);
    }
    std::this_thread::sleep_for(milliseconds(PrintFinalHoldMs));
    // The screen as it stood when the stickers came out, kept with them.
    if (!sheetDir.empty()) {
        const std::vector<uint8_t> img = osd_printing(grid, marks);
        std::vector<unsigned char> rgb(size_t(OsdW) * OsdH * 3u);
        for (size_t i = 0; i < size_t(OsdW) * OsdH; i++) {
            rgb[i * 3 + 0] = img[i * 4 + 0];
            rgb[i * 3 + 1] = img[i * 4 + 1];
            rgb[i * 3 + 2] = img[i * 4 + 2];
        }
        write_png(sheetDir / "printer_display.png", OsdW, OsdH, rgb.data());
    }
    snap_overlay_hide();
}

} // namespace
} // namespace snap

namespace snap {
namespace {

// ---------------------------------------------------------------------------
// The display mode's captures
// ---------------------------------------------------------------------------
std::set<std::filesystem::path> list_dumps() {
    std::set<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dumps_dir(), ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".bmp") {
            files.insert(entry.path());
        }
    }
    return files;
}

// Called with the mutex held. Arms the renderer's capture for the next
// present and remembers what was on disk before it.
void arm_capture(Station& s) {
    s.dumpsBefore = list_dumps();
    s.armedAt = std::chrono::steady_clock::now();
    s.armedAtFile = std::filesystem::file_time_type::clock::now() - std::chrono::seconds(1);
    s.capture = Capture::Armed;
    snap_frame_dump_station.store(1);
    snap_frame_dump_pending.store(1);
}

// Called with the mutex held on every 0xC000 read while a capture is armed.
// Finishes the slot when the presented frame has landed (or after four
// seconds without it), reads the VI framebuffer, writes the slot's files,
// and releases the game. The framebuffer is read here rather than at the
// 0x02 write so that the renderer's write-back of that frame has completed;
// the game keeps drawing the same picture while it waits.
void poll_capture(uint8_t* rdram, Station& s) {
    if (s.capture != Capture::Armed) {
        return;
    }
    const auto elapsed = std::chrono::steady_clock::now() - s.armedAt;
    std::filesystem::path arrived;
    if (snap_frame_dump_pending.load() <= 0 && elapsed > std::chrono::milliseconds(200)) {
        // A file that was not there at the arm AND was written after it: a
        // late encode of an earlier slot cannot be taken for this one.
        for (const auto& p : list_dumps()) {
            std::error_code ec;
            const auto written = std::filesystem::last_write_time(p, ec);
            if (!s.dumpsBefore.count(p) && !ec && written >= s.armedAtFile) {
                arrived = p;
            }
        }
    }
    if (arrived.empty() && elapsed < std::chrono::seconds(4)) {
        return;
    }
    snap_frame_dump_station.store(0);
    snap_frame_dump_pending.store(0);
    s.capture = Capture::Idle;

    Frame f;
    const int slot = int(s.frames.size()) + 1;
    char name[64];
    if (read_vi_frame(rdram, f)) {
        snprintf(name, sizeof(name), "slot_%02d.png", slot);
        // Encoded on its own thread: this runs on a guest thread, and the
        // runtime runs one guest thread at a time, so time spent here is
        // time the whole game stands still.
        std::thread([path = s.sheetDir / "slots" / name, w = f.width, h = f.height, rgb = f.rgb]() {
            write_png(path, w, h, rgb.data());
        }).detach();
    }
    if (!arrived.empty()) {
        snprintf(name, sizeof(name), "slot_%02d_presented.bmp", slot);
        std::error_code ec;
        std::filesystem::rename(arrived, s.sheetDir / "slots" / name, ec);
        if (!ec) {
            f.presented = s.sheetDir / "slots" / name;
        }
    } else {
        say("slot %d: the presented frame never arrived; the framebuffer alone was kept", slot);
    }
    say("slot %d of %d captured (%dx%d framebuffer%s)", slot, SlotCount, f.width, f.height,
        f.presented.empty() ? "" : ", presented frame kept");
    s.frames.push_back(std::move(f));
    // The printer's pace: the photo stays up while it captures, then its
    // grid; the read handler walks the phases from the game's status polls.
    s.slotPhase = 1;
    s.phaseUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(PhotoHoldMs);
}

// The game is released to draw the next photo while the grid is still up;
// the old photo would otherwise show again for the frames the new one takes.
// A watcher reads the framebuffer the VI is scanning out until it no longer
// looks like the slot just captured, then takes the grid down. Reads only,
// beside the game's thread: a torn read can only delay the answer a poll.
void slot_release(Station& s) {
    if (s.frames.empty() || (s.rdram == nullptr)) {
        snap_overlay_hide();
        return;
    }
    std::thread([rdram = s.rdram, last = s.frames.back().rgb, w = s.frames.back().width, h = s.frames.back().height]() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(NewPhotoWaitMs);
        while (std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            Frame f;
            if (!read_vi_frame(rdram, f) || (f.width != w) || (f.height != h) || (f.rgb.size() != last.size())) {
                continue;
            }
            uint64_t diff = 0;
            uint64_t n = 0;
            for (size_t i = 0; i + 2 < f.rgb.size(); i += 3 * 7) {
                diff += uint64_t(std::abs(int(f.rgb[i]) - int(last[i])));
                diff += uint64_t(std::abs(int(f.rgb[i + 1]) - int(last[i + 1])));
                diff += uint64_t(std::abs(int(f.rgb[i + 2]) - int(last[i + 2])));
                n += 3;
            }
            if ((n > 0) && (diff / n >= 12)) {
                break;
            }
        }
        snap_overlay_hide();
    }).detach();
}

void finish_sheet(Station& s) {
    if (s.frames.empty()) {
        say("display ended with no captures; nothing written");
        return;
    }
    // The framebuffer sheet: what the printer received.
    std::vector<const Frame*> frames;
    std::vector<const std::vector<unsigned char>*> pixels;
    int w = 0, h = 0;
    for (const Frame& f : s.frames) {
        if (f.width == 0) {
            continue;
        }
        if (w == 0) {
            w = f.width;
            h = f.height;
        }
        if (f.width == w && f.height == h) {
            frames.push_back(&f);
            pixels.push_back(&f.rgb);
        }
    }
    if (!pixels.empty() && compose_sheet(s.sheetDir / "sheet.png", frames, pixels, w, h)) {
        say("sheet written: %s (%d slots of %dx%d, %d across)", path_text(s.sheetDir / "sheet.png").c_str(),
            int(pixels.size()), w, h, SheetColumns);
    }
    // The presented sheet: the renderer's frames, converted from the BMPs.
    std::vector<std::vector<unsigned char>> presented;
    std::vector<const std::vector<unsigned char>*> presentedPtrs;
    int pw = 0, ph = 0;
    bool consistent = true;
    for (const Frame& f : s.frames) {
        if (f.presented.empty()) {
            consistent = false;
            break;
        }
        std::vector<unsigned char> rgb;
        int bw = 0, bh = 0;
        if (!read_bmp24(f.presented, bw, bh, rgb)) {
            consistent = false;
            break;
        }
        if (pw == 0) {
            pw = bw;
            ph = bh;
        }
        if (bw != pw || bh != ph) {
            consistent = false;
            break;
        }
        presented.push_back(std::move(rgb));
    }
    if (consistent && !presented.empty()) {
        for (const auto& p : presented) {
            presentedPtrs.push_back(&p);
        }
        if (compose_sheet(s.sheetDir / "sheet_presented.png", frames, presentedPtrs, pw, ph)) {
            say("presented sheet written: %s (%dx%d frames)", path_text(s.sheetDir / "sheet_presented.png").c_str(), pw, ph);
        }
        // The singles as PNG too; the BMPs were only the capture's transport.
        for (size_t i = 0; i < presented.size(); i++) {
            char name[64];
            snprintf(name, sizeof(name), "slot_%02zu_presented.png", i + 1);
            if (write_png(s.sheetDir / "slots" / name, pw, ph, presented[i].data())) {
                std::error_code ec;
                std::filesystem::remove(s.frames[i].presented, ec);
            }
        }
    } else if (!s.frames.empty()) {
        say("presented frames incomplete or of mixed sizes; no presented sheet");
    }
}

// ---------------------------------------------------------------------------
// The message bytes
// ---------------------------------------------------------------------------
void on_message(uint8_t* rdram, Station& s, uint8_t msg) {
    (void)rdram;
    s.lastMessage = msg;
    switch (msg) {
        case MsgSaving: {
            std::error_code ec;
            s.saveTimeAtCC = std::filesystem::last_write_time(base_path(SaveFileRel), ec);
            s.haveSaveTimeAtCC = !ec;
            // Every cartridge save is bracketed by CC and 33 while a station is
            // present (func_800BF244_5C0E4), the lab's Save included; only 5A
            // says a print was asked for.
            say("the game is writing its save (0xCC)");
            break;
        }
        case MsgSaved:
            say("the save is written (0x33)");
            break;
        case MsgResetPlease:
            if (!s.resetRequested) {
                s.resetRequested = true;
                s.busy = true;   // held until the reset, as the kiosk did
                say("reset requested; relaunching into the photo display once the save is on disk");
                schedule_relaunch("print", "the Snap Station print", 300, true);
            }
            break;
        case MsgDisplayStart: {
            s.frames.clear();
            s.sheetDir = base_path(OutputDirName) / timestamp_now();
            std::error_code ec;
            std::filesystem::create_directories(s.sheetDir, ec);
            say("photo display started; sheet folder %s", path_text(s.sheetDir).c_str());
            break;
        }
        case MsgPhotoShown:
            if (s.sheetDir.empty()) {
                s.sheetDir = base_path(OutputDirName) / timestamp_now();
            }
            {
                std::error_code ec;
                std::filesystem::create_directories(s.sheetDir / "slots", ec);
            }
            // The game says the next photo is on screen: whatever the
            // printer's display still shows comes down now, before the
            // capture, which must photograph the game's frame. Two slots in
            // a row usually show the same photo (each fills a 2x2 block), so
            // the watcher that looks for the picture to change cannot be the
            // only thing that ends the grid.
            snap_overlay_hide();
            s.slotPhase = 0;
            s.busy = true;
            arm_capture(s);
            break;
        case MsgDisplayDone:
            if (!s.frames.empty() || !s.sheetDir.empty()) {
                // The sheet is written on its own thread while the station
                // holds the game with 0x08, as the kiosk held it while it
                // printed; the echo of 0x04 comes when the files are on disk.
                s.busy = true;
                std::thread([]() {
                    Station& st2 = st();
                    std::string sheet;
                    std::vector<uint8_t> grid;
                    std::filesystem::path sheetDir;
                    {
                        std::lock_guard<std::mutex> lock(st2.mutex);
                        finish_sheet(st2);
                        sheet = path_text(st2.sheetDir);
                        sheetDir = st2.sheetDir;
                        grid = osd_grid(st2.frames);
                        st2.frames.clear();
                        st2.sheetDir.clear();
                        remove_marker();
                        st2.jobPending.store(false);
                    }
                    // The printer's screen, for as long as its three passes
                    // took; the game waits on the busy byte throughout, as it
                    // did in the kiosk.
                    say("printing: the printer's display is up for the three passes");
                    run_printing_display(grid, sheetDir);
                    {
                        std::lock_guard<std::mutex> lock(st2.mutex);
                        st2.busy = false;
                    }
                    // The stickers are handed over by this process, before it
                    // goes: the folder opens whether or not the relaunch after
                    // it comes up.
#if defined(_WIN32)
                    open_folder(sheet);
#endif
                    say("display finished; relaunching into a normal boot");
                    schedule_relaunch("restart", "the end of the print", 500, false);
                }).detach();
            }
            break;
        case MsgTooFewPhotos:
            say("the game reports fewer than four photos to print; the display cannot run");
            remove_marker();
            s.jobPending.store(false);
            schedule_relaunch("restart", "an abandoned print", 1500, false);
            break;
        default:
            say("message 0x%02X", msg);
            break;
    }
}

// Once the station has been present it stays present until the process
// ends: the game's printer queue exists only after a detection, and the
// display mode calls the printer without asking whether it is still there,
// so a device that vanished mid-job would hand the game stale bytes.
// Turning the setting off takes effect at the next boot.
bool present_now() {
    Station& s = st();
    if (s.jobPending.load() || s.everPresent.load()) {
        return true;
    }
    if (!s.enabled.load()) {
        return false;
    }
    if ((std::chrono::steady_clock::now() - s.start) > std::chrono::seconds(PresenceDelaySeconds)) {
        s.everPresent.store(true);
        return true;
    }
    return false;
}

#if defined(_WIN32)
void wait_for_process(unsigned long pid) {
    if (pid == 0 || pid == GetCurrentProcessId()) {
        return;
    }
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid));
    if (h == nullptr) {
        return;   // already gone
    }
    const DWORD waited = WaitForSingleObject(h, 15000);
    CloseHandle(h);
    if (waited == WAIT_TIMEOUT) {
        say("the previous instance (pid %lu) is still running after 15 s; continuing", pid);
    }
}
#endif

} // namespace

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------
void station_init() {
    Station& s = st();
    {
        std::lock_guard<std::mutex> lock(settings_mutex());
        s.enabled.store(settings().snap_station);
    }
    s.start = std::chrono::steady_clock::now();

    std::ifstream in(marker_path());
    if (!in) {
        return;
    }
    std::string mode;
    unsigned long pid = 0;
    long long when = 0;
    in >> mode >> pid >> when;
    std::string extra;
    std::getline(in, extra);   // the rest of the time line
    std::getline(in, extra);   // the sheet folder, when a print just ended
    in.close();
#if defined(_WIN32)
    wait_for_process(pid);
#endif
    const long long age = static_cast<long long>(std::time(nullptr)) - when;
    if (mode == "print" && age >= 0 && age < 600) {
        // The job is honoured whatever the setting says: the run that wrote
        // it had the station present, by the setting or by the title screen's
        // item, and the age check above is what guards against a stale one.
        s.jobPending.store(true);
        say("print job pending: the station is present from boot for the photo display");
    } else {
        // "restart", or a job too old to trust. The marker goes first, so
        // nothing that follows can leave it behind for the next boot.
        remove_marker();
#if defined(_WIN32)
        if (mode == "restart" && !extra.empty()) {
            open_folder(extra);
        }
#endif
    }
}

void station_request_from_title() {
    Station& s = st();
    if (!s.everPresent.exchange(true)) {
        say("chosen from the title screen: port 4 carries the station for the rest of this run");
    }
}

void station_set_enabled(bool enabled) {
    Station& s = st();
    const bool was = s.enabled.exchange(enabled);
    if (was != enabled) {
        say("%s", enabled ? "on: port 4 will carry the station" : "off: port 4 is empty");
    }
}

bool station_port4_present() {
    return present_now();
}

bool station_ram_write(uint8_t* rdram, int32_t channel, uint32_t address, gpr buffer, int32_t* result) {
    if (channel != StationChannel || !present_now()) {
        return false;
    }
    Station& s = st();
    std::lock_guard<std::mutex> lock(s.mutex);
    const uint8_t last = MEM_BU(BlockSize - 1, buffer);
    if (address == DetectBlock) {
        s.detectByte = last;
        say("probe: the game wrote 0x%02X to the detect block", last);
        *result = 0;
        return true;
    }
    if (address == MessageBlock) {
        on_message(rdram, s, last);
        *result = 0;
        return true;
    }
    return false;
}

bool station_ram_read(uint8_t* rdram, int32_t channel, uint32_t address, gpr buffer, int32_t* result) {
    if (channel != StationChannel || !present_now()) {
        return false;
    }
    Station& s = st();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (address == DetectBlock) {
        // A pak echoes the FE check; the station answers it with nothing and
        // echoes only its own id (jamchamb; decomp contInitialize).
        const uint8_t fill = (s.detectByte == PrinterId) ? PrinterId : uint8_t(0);
        for (uint32_t i = 0; i < BlockSize; i++) {
            MEM_B(i, buffer) = int8_t(fill);
        }
        say("probe: answered the detect block with 0x%02X%s", fill, (fill == PrinterId) ? " (recognised as the printer)" : "");
        (void)PakCheckByte;
        *result = 0;
        return true;
    }
    if (address == MessageBlock) {
        s.rdram = rdram;
        poll_capture(rdram, s);
        const auto now = std::chrono::steady_clock::now();
        if ((s.slotPhase == 1) && (now >= s.phaseUntil)) {
            const std::vector<uint8_t> grid = osd_grid(s.frames);
            snap_overlay_show(grid.data(), OsdW, OsdH);
            s.slotPhase = 2;
            s.phaseUntil = now + std::chrono::milliseconds(GridHoldMs);
        }
        else if ((s.slotPhase == 2) && (now >= s.phaseUntil)) {
            s.slotPhase = 0;
            s.busy = false;
            slot_release(s);
        }
        for (uint32_t i = 0; i < BlockSize - 1; i++) {
            MEM_B(i, buffer) = 0;
        }
        MEM_B(BlockSize - 1, buffer) = int8_t(s.busy ? BusyByte : s.lastMessage);
        *result = 0;
        return true;
    }
    return false;
}

} // namespace snap
