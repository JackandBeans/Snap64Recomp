/**
 * @file main.cpp
 * @brief WaveRace64-Recomp game integration entry point.
 *
 * Registers the Pokemon Snap GameEntry with the N64ModernRuntime,
 * builds the overlay section table, wires up all callbacks, and
 * calls recomp::start() to launch the game.
 */

#include <cstdio>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>
#include <utility>
#include <chrono>

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
namespace snap { extern uint8_t* g_rdram; }
extern "C" void snap_publish_ai_len(uint8_t* rdram);

// Pull in the recompiled function declarations and overlay tables.
#include "recomp_overlays.inl"

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

static void* create_gfx() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "[SNAP] SDL_Init failed: %s\n", SDL_GetError());
        return nullptr;
    }
    return nullptr; // gfx_data not used
}

static ultramodern::renderer::WindowHandle create_window(void* /*gfx_data*/) {
    sdl_window = SDL_CreateWindow(
        "Pokemon Snap",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 960,
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

    wh.window = wmInfo.info.win.window; wh.thread_id = GetCurrentThreadId(); return wh;
#else
    return sdl_window;
#endif
}

static void update_gfx(void* /*gfx_data*/) {
    // Publish SDL's real audio backlog where the game's patched AI_LEN read
    // (auThreadMain, vram 0x800219D8) now looks for it. Without this the game
    // believes the audio queue is always empty and synthesizes a full frame of
    // samples every tick, so music advances faster than wall-clock and the
    // queue overruns (sped-up, choppy audio).
    snap_publish_ai_len(snap::g_rdram);
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                ultramodern::quit();
                break;
            case SDL_KEYDOWN:
                if (snap::handle_settings_hotkey(event.key.keysym.scancode)) {
                    break;
                }
                if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    ultramodern::quit();
                }
                break;
            default:
                break;
        }
    }
}

// ---------------------------------------------------------------------------
// Events callbacks
// ---------------------------------------------------------------------------
static void vi_callback() {
    // Called each VI interrupt. Can be used for frame pacing.
}

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
            printf("[SNAP] ROM validated, starting game...\n");
            recomp::start_game(game_id);
        } else {
            fprintf(stderr, "[SNAP] ERROR: ROM not valid at startup (check pokemonsnap.z64 in CWD)\n");
        }
    }).detach();
}

// ---------------------------------------------------------------------------
// Error handling callback
// ---------------------------------------------------------------------------
static void error_message_box(const char* msg) {
    fprintf(stderr, "[SNAP] ERROR: %s\n", msg);
    if (sdl_window) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Pokemon Snap - Error", msg, sdl_window);
    }
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
// Reports the N64-space address behind an access violation. The recompiled code
// indexes RDRAM as (rdram + n64addr - 0x80000000), so the delta from the RDRAM
// base recovers the guest address the game actually asked for.
static LONG CALLBACK snap_veh(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        ULONG_PTR op   = ep->ExceptionRecord->ExceptionInformation[0];
        ULONG_PTR addr = ep->ExceptionRecord->ExceptionInformation[1];
        uintptr_t rip  = (uintptr_t)ep->ContextRecord->Rip;
        uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
        fprintf(stderr, "[SNAP-AV] %s host=0x%llX rva=0x%llX\n",
                op == 0 ? "read" : (op == 1 ? "write" : "exec"),
                (unsigned long long)addr, (unsigned long long)(rip - base));
        if (snap::g_rdram != nullptr) {
            long long d = (long long)((uintptr_t)addr - (uintptr_t)snap::g_rdram);
            fprintf(stderr, "[SNAP-AV] rdram_delta=0x%llX  guest_addr=0x%08llX\n",
                    (unsigned long long)d, (unsigned long long)(0x80000000ll + d));
        }
        fflush(stderr);
    }
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
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

#if defined(_WIN32)
    AddVectoredExceptionHandler(1, snap_veh);
#endif
    printf("[SNAP] Pokemon Snap PC Recompilation v0.1.0\n");

    snap::load_settings();
    snap::apply_graphics_settings();

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
        .project_version = { .major = 0, .minor = 1, .patch = 0, .suffix = "-alpha" },

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

    printf("[SNAP] Starting recomp runtime...\n");
    // Anchor config path to CWD so saves, mods, and ROM cache resolve correctly.
    recomp::register_config_path(std::filesystem::current_path());
    recomp::start(config);

    // Persist hotkey-driven settings changes (F7-F11) so they survive the exit.
    snap::save_settings();

    // Cleanup
    if (sdl_window) {
        SDL_DestroyWindow(sdl_window);
        sdl_window = nullptr;
    }
    SDL_Quit();

    printf("[SNAP] Exited cleanly.\n");
    return 0;
}






