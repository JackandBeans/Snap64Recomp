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

    __real_dmaLoadOverlay(rdram, ctx);

    if (rom_end > rom_start) {
        uint32_t size = rom_end - rom_start;
        // Evict whatever previously occupied the destination, then register
        // the sections covered by this ROM range at their new home.
        snap_prepare_overlay_load(static_cast<int32_t>(vram_start), size);
        load_overlays(rom_start, static_cast<int32_t>(vram_start), size);
        snap_record_overlay_load(rom_start, static_cast<int32_t>(vram_start), size);

        if (rom_start == 0x46270u) {
            snap::g_app_level_resident = true;
        }
        else if (vram_start < 0x801DC8C0u && (vram_start + size) > 0x8009A8C0u) {
            // Another overlay took over (part of) the in-level code region.
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

extern "C" void check_sp_imem(uint8_t* rdram, recomp_context* ctx) {
    snap::g_rdram = rdram;
    (void)ctx;
    MEM_B(0, (gpr)(int32_t)SNAP_SP_IMEM_OKAY) = 1;
}

extern "C" void check_sp_dmem(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    MEM_B(0, (gpr)(int32_t)SNAP_SP_DMEM_OKAY) = 1;
}


// Publishes SDL's real audio backlog to the scratch word that auThreadMain's
// patched AI_LEN read (vram 0x800219D8 -> 0x80700004) consumes. Lives here
// because the MEM_W macro requires a variable literally named dram.
extern "C" void snap_publish_ai_len(uint8_t* rdram) {
    if (rdram == nullptr) return;
    // ultramodern already models AI_LEN: queued bytes minus a lookahead margin.
    MEM_W(0, (gpr)(int32_t)0x80700004) = ultramodern::get_remaining_audio_bytes();
}



