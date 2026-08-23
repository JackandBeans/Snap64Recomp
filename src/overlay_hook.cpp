/**
 * @file overlay_hook.cpp
 * @brief Registers Pokemon Snap's overlays with librecomp as the game loads them.
 *
 * Every code overlay in Snap is loaded through dmaLoadOverlay(Overlay*), where
 * the struct provides the ROM range and destination VRAM. The linker --wrap
 * option routes all calls here; we run the real (recompiled) loader first,
 * then update librecomp's function tables for the newly resident code.
 */
#include <chrono>
#include <cstdint>
#include <cstdio>

#include "recomp.h"
#include "librecomp/overlays.hpp"
#include "settings.h"
#include "audio.h"
#include "ultramodern/ultramodern.hpp"
extern "C" void snap_prepare_overlay_load(int32_t ram_addr, uint32_t size);
extern "C" void snap_record_overlay_load(uint32_t rom, int32_t ram_addr, uint32_t size);
extern "C" {
#include "funcs.h"
}

namespace snap {
    extern uint8_t* g_rdram;
    // True while the in-level overlay group (ROM 0x46270) is resident. The
    // focus-dot gate in rt64_render_context.cpp reads .app_level bss addresses
    // that only hold meaningful data in that state; menu overlays and heap
    // allocations reuse the same RAM otherwise.
    bool g_app_level_resident = false;
}



static inline uint32_t read_u32(uint8_t* rdram, uint32_t addr) {
    // Same addressing the generated code uses for 32-bit loads.
    return *reinterpret_cast<uint32_t*>(rdram + (addr - 0x80000000u));
}

extern "C" void dmaLoadOverlay(uint8_t* rdram, recomp_context* ctx) {
    snap::g_rdram = rdram;
    snap::apply_game_settings(rdram);
    uint32_t overlay_addr = static_cast<uint32_t>(ctx->r4); // a0 = Overlay*

    uint32_t rom_start  = read_u32(rdram, overlay_addr + 0x00);
    uint32_t rom_end    = read_u32(rdram, overlay_addr + 0x04);
    uint32_t vram_start = read_u32(rdram, overlay_addr + 0x08);

    const auto dmaStart = std::chrono::steady_clock::now();
    __real_dmaLoadOverlay(rdram, ctx);
    {
        const int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - dmaStart).count();
        if (ms > 5) {
            printf("[SNAP-DMASTALL] overlay rom %08X..%08X took %lld ms\n",
                   rom_start, rom_end, (long long)ms);
            fflush(stdout);
        }
    }

    if (rom_end > rom_start) {
        uint32_t size = rom_end - rom_start;
        // Evict whatever previously occupied the destination, then register
        // the sections covered by this ROM range at their new home.
        snap_prepare_overlay_load(static_cast<int32_t>(vram_start), size);
        load_overlays(rom_start, static_cast<int32_t>(vram_start), size);
        snap_record_overlay_load(rom_start, static_cast<int32_t>(vram_start), size);

        // Whether the in-level code is still the code that is loaded. The test
        // used to call the level evicted whenever any load overlapped a wide
        // span of the level's region, and the game loads into that span
        // constantly while playing -- Pokemon, effects, the courses' own
        // pieces. So the level read as evicted within a frame or two of every
        // ride starting, and everything gated on "is this a cutscene" ran
        // during ordinary play: measured at sixty-four of every sixty-five
        // game frames. The renderer's transition holds are gated on exactly
        // that, so a system built to hide the intro's staged frames was
        // holding frames all the way around a course, showing the previous
        // picture again and again -- the doubled image and the hitching on
        // turns. A load only evicts the level now if it lands on the level's
        // own entry, which is what being replaced actually means.
        // Watch the overlay a ride actually runs from. This tracked the
        // rendering group (ROM 0x46270) instead, which loads once at boot and
        // is evicted by the very next load, so the answer was "not in a level"
        // from the first seconds of the process onwards -- including all the
        // way around a course. Everything gated on it was therefore gated on
        // nothing: the renderer's transition holds, built for the intro's
        // staged frames, ran during ordinary play and held one game frame in
        // eight, which is the doubled picture and the hitching on turns.
        //
        // The level's own code lives in ROM 0x4F0610 at 0x80350200 -- the
        // course update and both camera routines are inside it -- so its
        // arrival is the game entering a level and something loading over it
        // is the game leaving one. The gate it feeds reads level state, so
        // this is also the overlay it always meant.
        constexpr uint32_t LevelCodeStart = 0x80350200u;
        constexpr uint32_t LevelCodeEnd = 0x803AB1C0u;
        if (rom_start == 0x4F0610u) {
            snap::g_app_level_resident = true;
        }
        else if ((vram_start < LevelCodeEnd) && ((vram_start + size) > LevelCodeStart)) {
            snap::g_app_level_resident = false;
        }
        fprintf(stderr, "[SNAP] overlay: rom %06X..%06X -> %08X (%u bytes)\n",
               rom_start, rom_end, vram_start, size);
    }
}

// Pokemon Snap boot-time RSP memory probes. check_sp_imem expects to read the
// CIC seed 6103 from SP_IMEM (an anti-piracy check the volcano level later
// consults via gSPImemOkay/gSPDmemOkay). Hardware register space isn't mapped
// on PC, so satisfy the flags directly.
static constexpr uint32_t SNAP_SP_IMEM_OKAY = 0x800484E0;
static constexpr uint32_t SNAP_SP_DMEM_OKAY = 0x800484E1;

// Both report success in v0; leaving it unwritten would hand the caller
// whatever the previously executed recompiled function left behind.
extern "C" void check_sp_imem(uint8_t* rdram, recomp_context* ctx) {
    snap::g_rdram = rdram;
    MEM_B(0, (gpr)(int32_t)SNAP_SP_IMEM_OKAY) = 1;
    ctx->r2 = 0;
}

extern "C" void check_sp_dmem(uint8_t* rdram, recomp_context* ctx) {
    MEM_B(0, (gpr)(int32_t)SNAP_SP_DMEM_OKAY) = 1;
    ctx->r2 = 0;
}


// Publishes SDL's real audio backlog to the scratch word that auThreadMain's
// patched AI_LEN read (vram 0x800219D8 -> 0x80700004) consumes. Lives here
// because the MEM_W macro requires a variable literally named rdram.
extern "C" void snap_publish_ai_len(uint8_t* rdram) {
    if (rdram == nullptr) return;
    // ultramodern already models AI_LEN: queued bytes minus a lookahead margin.
    MEM_W(0, (gpr)(int32_t)0x80700004) = ultramodern::get_remaining_audio_bytes();
}



