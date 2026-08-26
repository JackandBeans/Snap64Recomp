/**
 * @file overlay_hook.cpp
 * @brief Registers Pokemon Snap's overlays with librecomp as the game loads them.
 *
 * Every code overlay in Snap is loaded through dmaLoadOverlay(Overlay*), where
 * the struct provides the ROM range and destination VRAM. The linker --wrap
 * option routes all calls here; we run the real (recompiled) loader first,
 * then update librecomp's function tables for the newly resident code.
 */
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
    // True while the code a ride runs from is the code that is loaded.
    // The renderer holds frames to hide the staged ticks of the movie the
    // console never displayed, and that is worth doing while a film is
    // playing and harmful while somebody is driving: a held frame is a
    // freeze and then a lurch of two or three game frames, which is
    // exactly what a player feels as a stutter.
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

    __real_dmaLoadOverlay(rdram, ctx);

    if (rom_end > rom_start) {
        uint32_t size = rom_end - rom_start;
        // Evict whatever previously occupied the destination, then register
        // the sections covered by this ROM range at their new home.
        snap_prepare_overlay_load(static_cast<int32_t>(vram_start), size);
        load_overlays(rom_start, static_cast<int32_t>(vram_start), size);
        snap_record_overlay_load(rom_start, static_cast<int32_t>(vram_start), size);

        // The course's own code lives in ROM 0x4F0610 at 0x80350200 -- the
        // course update and both camera routines are inside it -- so its
        // arrival is the game entering a level and something loading over
        // it is the game leaving one.
        //
        // The test used to watch a different overlay, one that loads once at
        // boot and is evicted by the very next load, so the answer was "not
        // in a level" from the first seconds onwards and everything gated on
        // it was gated on nothing. A load only counts as evicting the level
        // if it lands on the level's own entry, which is what being replaced
        // actually means.
        constexpr uint32_t LevelCodeStart = 0x80350200u;
        constexpr uint32_t LevelCodeEnd = 0x803AB1C0u;
        if (rom_start == 0x4F0610u) {
            snap::g_app_level_resident = true;
        }
        else if ((vram_start < LevelCodeEnd) && ((vram_start + size) > LevelCodeStart)) {
            snap::g_app_level_resident = false;
        }
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



