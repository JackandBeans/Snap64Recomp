/**
 * @file main.cpp
 * @brief Snap64 Recomp game integration entry point.
 *
 * Registers the Pokemon Snap GameEntry with the N64ModernRuntime,
 * builds the overlay section table, wires up all callbacks, and
 * calls recomp::start() to launch the game.
 */

#include <atomic>
#include <cstdio>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>
#include <utility>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <stdexcept>

#include "librecomp/game.hpp"
#include "librecomp/overlays.hpp"
#include "librecomp/sections.h"
#include "librecomp/addresses.hpp"

// Game-specific headers
#include "rt64_render_context.h"
#include "rsp_microcode.h"
#include "audio.h"
#include "input.h"
#include "settings.h"
#include "version.h"
#include "paths.h"
#include "snap_station.h"
namespace snap { extern uint8_t* g_rdram; }
extern "C" void snap_publish_ai_len(uint8_t* rdram);

// Pull in the recompiled function declarations and overlay tables.
#include "recomp_overlays.inl"

#if SNAP_HAS_PATCH_BIN
// The game-side patches (patches/src, recompiled into RecompiledPatches):
// their section table, and the bytes of the patch ELF -- code the recompiler
// turned into C, and the .data section IDO keeps every float literal and
// table in. librecomp copies those bytes to patch_rdram_start, where
// patch.ld links them, before the game runs. Without the copy a patch that
// read one of its own constants read zero: a comparison against 3.2f was a
// comparison against nothing, and the fade quad it should have widened
// stayed as it was.
#include "../RecompiledPatches/recomp_overlays.inl"
extern "C" const unsigned char snap_patches_bin[];
extern "C" const size_t snap_patches_bin_size;
static void snap_register_patches() {
    recomp::overlays::register_patches(reinterpret_cast<const char*>(snap_patches_bin), snap_patches_bin_size, section_table, ARRLEN(section_table));
}
#else
static void snap_register_patches() {}
#endif

// Forward-declare the recomp entrypoint (defined in RecompiledFuncs/funcs.h,
// already included transitively through recomp_overlays.inl -> funcs.h).

// ---------------------------------------------------------------------------
// Pokemon Snap (USA Rev 1) constants
// ---------------------------------------------------------------------------
static constexpr uint64_t SNAP_ROM_HASH          = 0x73CBBC5C7DE9425Cull;
static constexpr gpr      SNAP_ENTRYPOINT_ADDR   = (gpr)(int32_t)0x80000400u;
static constexpr const char* SNAP_INTERNAL_NAME  = "POKEMON SNAP";
// Game uses 4-Kbit EEPROM for saves.


// ---------------------------------------------------------------------------
// SDL2 window / gfx callbacks
// ---------------------------------------------------------------------------
#include <SDL2/SDL.h>
#if defined(_WIN32)
#include <SDL2/SDL_syswm.h>
#endif

static SDL_Window* sdl_window = nullptr;
static void snap_update_window_title();

static void* create_gfx() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        // Nothing downstream can work without SDL, and the failures it would
        // produce (no window, no native handle, no renderer) all read as
        // unrelated bugs. Say what actually happened and stop.
        fprintf(stderr, "[SNAP] SDL_Init failed: %s\n", SDL_GetError());
        fflush(stderr);
        std::exit(1);
    }
    return nullptr; // gfx_data not used
}

static ultramodern::renderer::WindowHandle create_window(void* /*gfx_data*/) {
    // SNAP_WINDOW=WxH opens at an exact size. A replayed ride only reproduces
    // a player's GPU load if it renders at the player's resolution, and the
    // render scale follows the window, so a performance question can only be
    // investigated at the size it was reported at.
    int windowW = 1280, windowH = 960;
    if (const char* size = getenv("SNAP_WINDOW")) {
        int w = 0, h = 0;
        if ((sscanf(size, "%dx%d", &w, &h) == 2) && (w >= 320) && (h >= 240)) {
            windowW = w;
            windowH = h;
        }
    }
    sdl_window = SDL_CreateWindow(
        SNAP_PORT_NAME,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        windowW, windowH,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!sdl_window) {
        fprintf(stderr, "[SNAP] SDL_CreateWindow failed: %s\n", SDL_GetError());
    }
#if defined(_WIN32)
    // Report the real failure here. Handing RT64 an unwritten stack value as
    // the native window turns a clear SDL error into an unrelated-looking
    // swapchain crash much further downstream.
    ultramodern::renderer::WindowHandle wh{};
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (!sdl_window || SDL_GetWindowWMInfo(sdl_window, &wmInfo) != SDL_TRUE) {
        fprintf(stderr, "[SNAP] Unable to obtain a native window handle: %s\n", SDL_GetError());
        return wh;
    }

    snap_update_window_title();
    wh.window = wmInfo.info.win.window; wh.thread_id = GetCurrentThreadId(); return wh;
#else
    return sdl_window;
#endif
}

// Frame interpolation is the one setting that changes how the game looks
// rather than how it is presented, and a saved snapsettings.json silently
// outranks the built-in default. Put its state where it cannot be missed:
// from a shortcut the log is a file nobody reads until something is wrong.
static void snap_update_window_title() {
    if (sdl_window == nullptr) {
        return;
    }

    char title[160];
    snprintf(title, sizeof(title), "%s %s%s%s%s", SNAP_PORT_NAME, SNAP_PORT_VERSION,
             (snap::settings().fps_mode == 0) ? "" : " - interpolation ON (F8)",
             snap::settings().render_to_ram ? "" : " - render-to-RAM OFF (F6)",
             snap::settings().interpolate_camera ? "" : " - camera lerp OFF (F4)");
    SDL_SetWindowTitle(sdl_window, title);
}

static void update_gfx(void* /*gfx_data*/) {
    // Publish SDL's real audio backlog where the game's patched AI_LEN read
    // (auThreadMain, vram 0x800219D8) now looks for it. Without this the game
    // believes the audio queue is always empty and synthesizes a full frame of
    // samples every tick, so music advances faster than wall-clock and the
    // queue overruns (sped-up, choppy audio).
    snap_publish_ai_len(snap::g_rdram);

    // The Snap Station's relaunches come back at the window state the run
    // had. A boot is always windowed (settings.cpp), so the return to
    // fullscreen goes through the live path the maximize button uses,
    // once the window has been up for a moment.
    {
        static bool restoreChecked = false;
        static bool restorePending = false;
        static std::chrono::steady_clock::time_point restoreAt;
        const auto now = std::chrono::steady_clock::now();
        if (!restoreChecked) {
            restoreChecked = true;
            restorePending = snap::station_take_fullscreen_restore();
            restoreAt = now + std::chrono::milliseconds(1200);
        }
        if (restorePending && (sdl_window != nullptr) && (now >= restoreAt)) {
            restorePending = false;
            {
                std::lock_guard<std::mutex> lock(snap::settings_mutex());
                snap::settings().fullscreen = true;
            }
            snap::apply_graphics_settings();
            snap_update_window_title();
            printf("[SNAP] fullscreen restored after the Snap Station's relaunch\n");
            fflush(stdout);
        }
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                // Named in the log because an unattended session was seen
                // exiting gracefully with no one at the keyboard; whichever
                // path fires is the lead.
                printf("[SNAP] quit: SDL_QUIT event\n");
                fflush(stdout);
                ultramodern::quit();
                break;
            case SDL_KEYDOWN:
                if (snap::handle_settings_hotkey(event.key.keysym.scancode)) {
                    snap_update_window_title();
                    break;
                }
                if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    printf("[SNAP] quit: Escape (repeat=%d timestamp=%u)\n",
                           event.key.repeat, event.key.timestamp);
                    fflush(stdout);
                    ultramodern::quit();
                }
                break;
            case SDL_WINDOWEVENT:
                // The maximize button is the fullscreen switch: undo the
                // maximize so the windowed state underneath stays normal,
                // then enter fullscreen through the same live path the
                // GRAPHICS page uses. Turning the setting off in that page
                // (or pressing it again after a menu toggle) returns here.
                if (event.window.event == SDL_WINDOWEVENT_MAXIMIZED) {
                    if (sdl_window != nullptr) {
                        SDL_RestoreWindow(sdl_window);
                    }
                    {
                        std::lock_guard<std::mutex> lock(snap::settings_mutex());
                        snap::settings().fullscreen = true;
                    }
                    snap::apply_graphics_settings();
                }
                // The SOUND page's background mute follows these; the mute
                // itself zero-fills in the audio sink so the queue keeps
                // draining and the game's pacing never notices.
                else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                    snap::set_window_focused(false);
                }
                else if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                    snap::set_window_focused(true);
                }
                break;
            default:
                break;
        }
    }

    // The GRAPHICS and SOUND pages and the hotkeys only mark the settings
    // dirty; the file is written here, on the main thread, once the edits
    // have stopped for the debounce interval (settings.h).
    snap::settings_flush_if_due(std::chrono::steady_clock::now());
}

// ---------------------------------------------------------------------------
// Events callbacks
// ---------------------------------------------------------------------------
static void vi_callback() {
    // Called each VI interrupt. Can be used for frame pacing.
}

static void error_message_box(const char* msg);

static void gfx_init_callback() {
    // Called when the graphics subsystem is fully initialized.
    // We launch a detached thread that waits for the VI thread to complete
    // several dummy-VI iterations (populating both ViState slots) before
    // calling start_game(). Calling start_game() immediately here would race
    // with the VI thread: update_vi() dereferences next_state->mode which is
    // null until set_dummy_vi() has run at least once in the VI thread.
    std::thread([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::u8string game_id = u8"pokemonsnap";
        if (recomp::is_rom_valid(game_id)) {
            recomp::start_game(game_id);
        } else {
            // The ROM check in recomp::start already told the player exactly
            // what is wrong with pokemonsnap.z64 (missing, unreadable, not a
            // ROM, or the wrong dump with both hashes) in a dialog shown
            // before this window existed. A second, vaguer dialog here would
            // only bury that report. The window stays up until they close it.
            fprintf(stderr, "[SNAP] ROM check failed at startup; the game was not started.\n");
        }
    }).detach();
}

// ---------------------------------------------------------------------------
// Error handling callback
// ---------------------------------------------------------------------------
static void error_message_box(const char* msg) {
    fprintf(stderr, "[SNAP] ERROR: %s\n", msg);
    if (sdl_window) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, SNAP_PORT_NAME " - Error", msg, sdl_window);
        return;
    }
#if defined(_WIN32)
    // Before the window exists (the ROM check in recomp::start runs first)
    // there is nothing for SDL to parent a dialog to, and stderr is invisible
    // on the launch path a player uses, so show a plain Win32 dialog instead.
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, msg, -1, nullptr, 0);
    if (wide_len > 0) {
        std::wstring wide(static_cast<size_t>(wide_len), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, msg, -1, wide.data(), wide_len);
        MessageBoxW(nullptr, wide.c_str(), SNAP_PORT_NAME_W L" - Error", MB_OK | MB_ICONERROR);
    }
#endif
}

// ---------------------------------------------------------------------------
// Threads callback
// ---------------------------------------------------------------------------
static std::string get_game_thread_name(const OSThread* t) {
    int id = t ? static_cast<int>(t->id) : -1;
    switch (id) {
        case 1: return "SNAP Idle";
        case 3: return "SNAP Main";
        case 4: return "SNAP Audio";
        case 5: return "SNAP Sched";
        default: return "SNAP Thread " + std::to_string(id);
    }
}

// ---------------------------------------------------------------------------
// main()
// ---------------------------------------------------------------------------
#if defined(_WIN32)
// True when every byte of [p, p+len) sits in committed, readable memory right
// now. The dump below reads heap structures and the faulting thread's stacks,
// and a fault inside the fault handler would take the report with it.
static bool snap_veh_readable(const void* p, size_t len) {
    MEMORY_BASIC_INFORMATION mbi{};
    const uint8_t* cur = (const uint8_t*)p;
    const uint8_t* end = cur + len;
    while (cur < end) {
        if (VirtualQuery(cur, &mbi, sizeof(mbi)) == 0) return false;
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
        if (!(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                             PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return false;
        cur = (const uint8_t*)mbi.BaseAddress + mbi.RegionSize;
    }
    return true;
}

// Reports everything knowable about an access violation: the raw exception,
// what the OS says about the faulting page (the protection answer), the host
// registers, the guest CPU context if one is recoverable, and both stacks as
// return-address candidates. The recompiled code indexes RDRAM as
// (rdram + n64addr - 0xFFFFFFFF80000000), so the delta from the RDRAM base
// recovers the guest address the game actually asked for.
static LONG CALLBACK snap_veh(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    // A stuck loop that faults every iteration must not turn the log into the
    // whole story of the hang; the first few faults carry all the information.
    static std::atomic<int> reported{0};
    int n = reported.fetch_add(1);
    if (n >= 4) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const ULONG_PTR op   = ep->ExceptionRecord->ExceptionInformation[0];
    const ULONG_PTR addr = ep->ExceptionRecord->ExceptionInformation[1];
    CONTEXT* c = ep->ContextRecord;
    const uintptr_t rip  = (uintptr_t)c->Rip;
    const uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    fprintf(stderr, "[SNAP-AV] #%d tid=%lu %s host=0x%llX rva=0x%llX flags=0x%lX\n",
            n, GetCurrentThreadId(),
            op == 0 ? "read" : (op == 1 ? "write" : "exec"),
            (unsigned long long)addr, (unsigned long long)(rip - base),
            ep->ExceptionRecord->ExceptionFlags);

    // What the OS itself says about the faulting page. If State/Protect show
    // committed readable memory, the exception record and this query disagree
    // and the fault came from somewhere stranger than a protection miss.
    {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery((void*)addr, &mbi, sizeof(mbi)) != 0) {
            fprintf(stderr, "[SNAP-AV] page: allocBase=0x%llX state=0x%lX protect=0x%lX regionSize=0x%llX\n",
                    (unsigned long long)(uintptr_t)mbi.AllocationBase, mbi.State, mbi.Protect,
                    (unsigned long long)mbi.RegionSize);
        }
        if (snap::g_rdram != nullptr) {
            MEMORY_BASIC_INFORMATION rmbi{};
            VirtualQuery(snap::g_rdram, &rmbi, sizeof(rmbi));
            long long d = (long long)((uintptr_t)addr - (uintptr_t)snap::g_rdram);
            fprintf(stderr, "[SNAP-AV] rdram=0x%llX (allocBase=0x%llX) delta=0x%llX guest=0x%08llX %s\n",
                    (unsigned long long)(uintptr_t)snap::g_rdram,
                    (unsigned long long)(uintptr_t)rmbi.AllocationBase,
                    (unsigned long long)d, (unsigned long long)(0x80000000ll + d),
                    (mbi.AllocationBase == rmbi.AllocationBase) ? "(inside rdram allocation)" : "(NOT in rdram allocation)");
        }
    }

    fprintf(stderr, "[SNAP-AV] rax=%llX rbx=%llX rcx=%llX rdx=%llX rsi=%llX rdi=%llX rbp=%llX rsp=%llX\n",
            c->Rax, c->Rbx, c->Rcx, c->Rdx, c->Rsi, c->Rdi, c->Rbp, c->Rsp);
    fprintf(stderr, "[SNAP-AV] r8=%llX r9=%llX r10=%llX r11=%llX r12=%llX r13=%llX r14=%llX r15=%llX\n",
            c->R8, c->R9, c->R10, c->R11, c->R12, c->R13, c->R14, c->R15);

    // Recompiled functions keep ctx in a callee-saved register; try the usual
    // suspects and validate by shape: 32 guest GPRs, r0 == 0, r29 looking like
    // a sign-extended KSEG0 stack pointer.
    const uint64_t regs[] = { c->Rbx, c->Rsi, c->Rdi, c->R12, c->R13, c->R14, c->R15, c->Rbp };
    for (uint64_t cand : regs) {
        if (cand < 0x10000 || !snap_veh_readable((void*)cand, 32 * 8)) continue;
        const uint64_t* g = (const uint64_t*)cand;
        const uint64_t sp = g[29];
        if (g[0] == 0 && (sp >> 32) == 0xFFFFFFFFull && (uint32_t)sp >= 0x80000000u && (uint32_t)sp < 0xC0000000u) {
            fprintf(stderr, "[SNAP-AV] guest ctx@0x%llX: sp=0x%08X ra=0x%08X a0=0x%08X v0=0x%08X\n",
                    (unsigned long long)cand, (uint32_t)g[29], (uint32_t)g[31], (uint32_t)g[4], (uint32_t)g[2]);
            // Return-address candidates on the guest stack place the fault in
            // the game's own call tree.
            if (snap::g_rdram != nullptr) {
                const uint32_t gsp = (uint32_t)sp;
                const uint8_t* host_sp = snap::g_rdram + (gsp - 0x80000000u);
                if (snap_veh_readable(host_sp, 0x200)) {
                    fprintf(stderr, "[SNAP-AV] guest stack @0x%08X:", gsp);
                    int printed = 0;
                    for (int i = 0; i < 0x200 / 4 && printed < 16; i++) {
                        uint32_t w = *(const uint32_t*)(host_sp + i * 4);
                        if (w >= 0x80000400u && w < 0x80800000u) {
                            fprintf(stderr, " +%X:%08X", i * 4, w);
                            printed++;
                        }
                    }
                    fprintf(stderr, "\n");
                }
            }
            break;
        }
    }

    // Host return-address candidates: anything on this thread's stack that
    // points into the executable, symbolizable offline against the .map. The
    // image extent comes straight from the PE header to avoid a psapi import
    // inside a fault handler.
    {
        const uintptr_t lo = base;
        const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
        const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
        const uintptr_t hi = lo + nt->OptionalHeader.SizeOfImage;
        {
            const uint64_t* sp = (const uint64_t*)c->Rsp;
            fprintf(stderr, "[SNAP-AV] host stack rvas:");
            int printed = 0;
            for (int i = 0; i < 512 && printed < 24; i++) {
                if (!snap_veh_readable(sp + i, 8)) break;
                uint64_t v = sp[i];
                if (v >= lo && v < hi) {
                    fprintf(stderr, " %llX", (unsigned long long)(v - lo));
                    printed++;
                }
            }
            fprintf(stderr, "\n");
        }
    }
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

// Snap stacks differently-sized overlays at overlapping addresses (a 0x6A00
// menu inside the 0x14160 .world region; .oaks_lab starting 0x20 above it).
// librecomp refuses partial unloads, so mirror its bookkeeping: remember what
// we actually loaded, and evict only resident sections, each by its own extent.
static std::vector<std::pair<uint32_t, uint32_t>> g_resident_overlays;

extern "C" void snap_prepare_overlay_load(int32_t ram_addr, uint32_t size) {
    const uint32_t n_start = (uint32_t)ram_addr;
    const uint32_t n_end   = n_start + size;
    for (auto it = g_resident_overlays.begin(); it != g_resident_overlays.end();) {
        const uint32_t s_start = it->first;
        const uint32_t s_end   = it->first + it->second;
        if (s_start < n_end && n_start < s_end) {
            unload_overlays((int32_t)it->first, it->second);
            it = g_resident_overlays.erase(it);
        } else {
            ++it;
        }
    }
}

extern "C" void snap_record_overlay_load(uint32_t rom, int32_t ram_addr, uint32_t size) {
    for (size_t i = 0; i < NUM_CODE_SECTIONS; i++) {
        const SectionTableEntry& s = code_sections[i];
        if (s.size == 0 || s.rom_addr >= 0xF0000000u) continue;
        if (s.rom_addr >= rom && s.rom_addr < rom + size) {
            g_resident_overlays.emplace_back(s.rom_addr - rom + (uint32_t)ram_addr, s.size);
        }
    }
}
#if defined(_WIN32)
// Where the log goes. The executable is a windowed program (CMakeLists.txt:
// /SUBSYSTEM:WINDOWS), so a shortcut launch opens no console and nothing
// would show a printf. Decided once, before the first line is printed:
//   1. stdout is already a handle the parent gave this process -- a pipe or
//      a file from a shell redirection, the headless test rigs, a Python
//      capture -- keep it, so `> out.log` reads exactly as before.
//   2. No handle, but the parent has a console (the name typed into cmd or
//      PowerShell): attach to it, so the lines appear there. The shell does
//      not wait for a windowed program, so its prompt interleaves; that is
//      how every windowed program behaves.
//   3. Neither (Explorer, a shortcut, Start): write snap64.log beside the
//      executable, keeping the previous run as snap64.prev.log so a crash
//      log survives one relaunch. Both streams are opened in append mode on
//      the same file and unbuffered, so their lines keep their order and the
//      last line before a crash is on disk.
static void snap_bind_stdio() {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if ((out != nullptr) && (out != INVALID_HANDLE_VALUE) && (GetFileType(out) != FILE_TYPE_UNKNOWN)) {
        return;
    }
    FILE* f = nullptr;
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        setvbuf(stdout, nullptr, _IONBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
        return;
    }
    const std::filesystem::path log = snap::base_path("snap64.log");
    const std::filesystem::path prev = snap::base_path("snap64.prev.log");
    std::error_code ec;
    std::filesystem::remove(prev, ec);
    // A process the Snap Station relaunched starts while the one that
    // launched it is still quitting and still holds snap64.log, so the
    // rename fails with a sharing violation. That parent releases the file
    // the moment it has started this one (snap_station.cpp, relaunch_self),
    // so waiting is enough; the cap is for a parent that hangs on exit.
    for (int attempt = 0; attempt < 200; attempt++) {
        std::filesystem::rename(log, prev, ec);
        if (!ec || !std::filesystem::exists(log, ec)) {
            break;
        }
        Sleep(100);
    }
    std::filesystem::remove(log, ec);
    if (_wfreopen_s(&f, log.c_str(), L"a", stdout) != 0) {
        return;
    }
    _wfreopen_s(&f, log.c_str(), L"a", stderr);
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
}
#endif

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

#if defined(_WIN32)
    // One copy at a time. A second launch from the same folder would race
    // the first for the save file and the log. The Snap Station's relaunch
    // starts the next copy while this one is still quitting, so a newcomer
    // waits for the holder to exit rather than refusing at once; only a
    // holder that is still running after the wait means a real second copy.
    {
        HANDLE instance = CreateMutexW(nullptr, FALSE, L"Local\\Snap64Recomp.instance");
        if (instance != nullptr) {
            const DWORD wait = WaitForSingleObject(instance, 25000);
            if ((wait != WAIT_OBJECT_0) && (wait != WAIT_ABANDONED)) {
                error_message_box("Snap64 Recomp is already running. Close the other window first.");
                return 0;
            }
        }
    }

    snap_bind_stdio();
    AddVectoredExceptionHandler(1, snap_veh);

    // Declare per-monitor DPI awareness before any window exists. Without
    // this, a display scaled above 100% -- most laptops -- makes Windows
    // treat the process as DPI-unaware and bitmap-stretch its output: the
    // picture blurs, sits off center, and shows stretched garbage at the
    // edges, none of which the renderer ever drew. A 100% display shows
    // nothing, which is why it survived every capture on the dev machine
    // while being plainly visible on a scaled one.
    {
        typedef BOOL(WINAPI *SetDpiCtxFn)(HANDLE);
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        SetDpiCtxFn setCtx = user32 ? (SetDpiCtxFn)GetProcAddress(user32, "SetProcessDpiAwarenessContext") : nullptr;
        // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (HANDLE)-4.
        if ((setCtx == nullptr) || !setCtx((HANDLE)-4)) {
            SetProcessDPIAware();
        }
    }
#endif
    printf("[SNAP] " SNAP_PORT_NAME " " SNAP_PORT_VERSION " (" SNAP_PORT_CODENAME ")\n");
    // The first thing a support log needs: where this run reads and writes.
    printf("[SNAP] data directory: %s\n",
           reinterpret_cast<const char*>(snap::base_dir().u8string().c_str()));

    snap::load_settings();
    // Before anything else opens the save or the caches: a relaunch after a
    // Snap Station print waits here for the previous instance to exit.
    snap::station_init();
    snap::apply_graphics_settings();
    snap::set_master_volume(snap::settings().master_volume);
    snap::set_mute_unfocused(snap::settings().mute_unfocused);

    // -----------------------------------------------------------------------
    // 1. Register overlay sections
    // -----------------------------------------------------------------------
    recomp::overlays::overlay_section_table_data_t section_table {
        .code_sections    = code_sections,
        .num_code_sections = NUM_CODE_SECTIONS,
        .total_num_sections = TOTAL_NUM_SECTIONS,
    };

    // Overlay-by-index mapping (not needed if overlay loading uses ROM address matching).
    recomp::overlays::overlays_by_index_t overlays_by_index {
        .table = nullptr,
        .len   = 0,
    };

    recomp::overlays::register_overlays(section_table, overlays_by_index);
    snap_register_patches();

    // -----------------------------------------------------------------------
    // 2. Register the game entry
    // -----------------------------------------------------------------------
    recomp::GameEntry snap_entry {
        .rom_hash            = SNAP_ROM_HASH,
        .internal_name       = SNAP_INTERNAL_NAME,
        .game_id             = u8"pokemonsnap",
        .mod_game_id         = "pokemonsnap",
        .save_type           = recomp::SaveType::AllowAll, // boot probes FlashRAM before using EEPROM
        .is_enabled          = true,
        .entrypoint_address  = SNAP_ENTRYPOINT_ADDR,
        .entrypoint          = recomp_entrypoint,
    };

    if (!recomp::register_game(snap_entry)) {
        fprintf(stderr, "[SNAP] Failed to register game entry!\n");
        return 1;
    }
    printf("[SNAP] Game registered: %s (hash 0x%016llX)\n",
           snap_entry.internal_name.c_str(),
           (unsigned long long)snap_entry.rom_hash);

    // -----------------------------------------------------------------------
    // 3. Build the Configuration and start
    // -----------------------------------------------------------------------
    recomp::Configuration config {
        .project_version = { .major = SNAP_VERSION_MAJOR, .minor = SNAP_VERSION_MINOR,
                             .patch = SNAP_VERSION_PATCH, .suffix = SNAP_VERSION_SUFFIX },

        .window_handle = ultramodern::renderer::WindowHandle{},

        .rsp_callbacks = {
            .get_rsp_microcode = snap::get_rsp_microcode,
        },

        .renderer_callbacks = {
            .create_render_context = snap::create_render_context,
        },

        .audio_callbacks = {
            .queue_samples       = snap::audio_queue_samples,
            .get_frames_remaining = snap::audio_get_frames_remaining,
            .set_frequency       = snap::audio_set_frequency,
        },

        .input_callbacks = {
            .poll_input              = snap::input_poll,
            .get_input               = snap::input_get,
            .set_rumble              = snap::input_set_rumble,
            .get_connected_device_info = snap::input_get_connected_device_info,
        },

        .gfx_callbacks = {
            .create_gfx    = create_gfx,
            .create_window = create_window,
            .update_gfx    = update_gfx,
        },

        .events_callbacks = {
            .vi_callback       = vi_callback,
            .gfx_init_callback = gfx_init_callback,
        },

        .error_handling_callbacks = {
            .message_box = error_message_box,
        },

        .threads_callbacks = {
            .get_game_thread_name = get_game_thread_name,
        },

        .message_queue_control = {
            // Pokemon Snap defaults: requeue timer, sp, si, dp; don't requeue ai, vi, pi.
        },
    };

    // The ROM (pokemonsnap.z64), saves/, mods/, mod_config/ and mods.json all
    // hang off this one path inside librecomp (recomp.cpp, pi.cpp). It is the
    // executable's directory, never the working directory: a shortcut with a
    // different "Start in" used to lose all of them.
    recomp::register_config_path(snap::base_dir());
    try {
        recomp::start(config);
    }
    catch (const std::exception& e) {
        // The renderer's setup throws when the graphics device cannot be
        // created (no Direct3D 12, a driver that refuses). Say so, in a
        // dialog and in the log, instead of dying silently.
        std::string msg = std::string("The game could not start: ") + e.what() +
            "\n\nIf this is a graphics error, update the GPU driver, or set \"graphics_api\": 1 in snapsettings.json to try Vulkan.";
        error_message_box(msg.c_str());
        return 1;
    }

    // An edit inside the last debounce window (a hotkey, or a page edit on
    // the game thread) has not reached the disk yet; write it now. Nothing
    // dirty, nothing written: a file edited by hand while the game ran is
    // left alone, and a clean exit does not churn the .bak.
    if (snap::settings_dirty()) {
        snap::save_settings();
    }

    // Cleanup
    if (sdl_window) {
        SDL_DestroyWindow(sdl_window);
        sdl_window = nullptr;
    }
    SDL_Quit();

    return 0;
}






