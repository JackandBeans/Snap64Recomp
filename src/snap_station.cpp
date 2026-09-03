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
void write_marker(const char* mode) {
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
    out << mode << "\n" << pid << "\n" << std::time(nullptr) << "\n";
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
    say("relaunched for %s; this instance is quitting", why);
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

void schedule_relaunch(const char* mode, const char* why, int delayMs, bool settleSave) {
    std::thread([mode, why, delayMs, settleSave]() {
        if (settleSave) {
            wait_for_save_to_settle();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        write_marker(mode);
        relaunch_self(why);
    }).detach();
}

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
        std::thread([path = s.sheetDir / name, w = f.width, h = f.height, rgb = f.rgb]() {
            write_png(path, w, h, rgb.data());
        }).detach();
    }
    if (!arrived.empty()) {
        snprintf(name, sizeof(name), "slot_%02d_presented.bmp", slot);
        std::error_code ec;
        std::filesystem::rename(arrived, s.sheetDir / name, ec);
        if (!ec) {
            f.presented = s.sheetDir / name;
        }
    } else {
        say("slot %d: the presented frame never arrived; the framebuffer alone was kept", slot);
    }
    say("slot %d of %d captured (%dx%d framebuffer%s)", slot, SlotCount, f.width, f.height,
        f.presented.empty() ? "" : ", presented frame kept");
    s.frames.push_back(std::move(f));
    s.busy = false;
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
            if (write_png(s.sheetDir / name, pw, ph, presented[i].data())) {
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
                std::error_code ec;
                std::filesystem::create_directories(s.sheetDir, ec);
            }
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
                    {
                        std::lock_guard<std::mutex> lock(st2.mutex);
                        finish_sheet(st2);
                        st2.frames.clear();
                        st2.sheetDir.clear();
                        remove_marker();
                        st2.jobPending.store(false);
                        st2.busy = false;
                    }
                    say("display finished; relaunching into a normal boot");
                    schedule_relaunch("restart", "the end of the print", 1500, false);
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
    in.close();
#if defined(_WIN32)
    wait_for_process(pid);
#endif
    const long long age = static_cast<long long>(std::time(nullptr)) - when;
    if (mode == "print" && age >= 0 && age < 600) {
        if (!s.enabled.load()) {
            say("a print job is pending but the Snap Station is off; the job is dropped");
            remove_marker();
            return;
        }
        s.jobPending.store(true);
        say("print job pending: the station is present from boot for the photo display");
    } else {
        // "restart", or a job too old to trust.
        remove_marker();
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
        poll_capture(rdram, s);
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
