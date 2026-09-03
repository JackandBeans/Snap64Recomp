/**
 * @file overlay_hook.cpp
 * @brief Registers Pokemon Snap's overlays with librecomp as the game loads them.
 *
 * Every code overlay in Snap is loaded through dmaLoadOverlay(Overlay*), where
 * the struct provides the ROM range and destination VRAM. MSVC has no linker
 * --wrap, so tools/hook_funcs.py renames the recompiled definition to
 * __real_dmaLoadOverlay and this file defines dmaLoadOverlay in its place:
 * we run the real (recompiled) loader first, then update librecomp's
 * function tables for the newly resident code. The same renaming wraps
 * dmaReadVPK0, the game's segment decompressor, so the port can harvest its
 * menu font the moment the main menu's segment is in RDRAM.
 */
#include <atomic>
#include <cstdint>
#include <cstdio>

#include "recomp.h"
#include "librecomp/overlays.hpp"
#include "settings.h"
#include "menu_harvest.h"
#include "audio.h"
#include "ultramodern/ultramodern.hpp"
extern "C" void snap_prepare_overlay_load(int32_t ram_addr, uint32_t size);
extern "C" void snap_record_overlay_load(uint32_t rom, int32_t ram_addr, uint32_t size);
extern "C" {
#include "funcs.h"
}

// The patch recompile emits plain recomp-style calls to the OS functions it
// uses, while librecomp exports them under the _recomp suffix the game's own
// recompile was configured to call. One bridge per function the patches need.
extern "C" void osSetIntMask_recomp(uint8_t* rdram, recomp_context* ctx);
extern "C" void osSetIntMask(uint8_t* rdram, recomp_context* ctx) {
    osSetIntMask_recomp(rdram, ctx);
}

namespace snap {
    extern uint8_t* g_rdram;
    // True while the code a ride runs from is the code that is loaded.
    // The renderer holds frames to hide the staged ticks of the movie the
    // console never displayed, and that is worth doing while a film is
    // playing and harmful while somebody is driving: a held frame is a
    // freeze and then a lurch of two or three game frames, which is
    // exactly what a player feels as a stutter.
    std::atomic<bool> g_app_level_resident = { false };

    // Lets a frame be held inside a course, which the gate below otherwise
    // forbids. Off by default, because that is the behaviour every measurement
    // so far was taken against.
    //
    // The gate says "hold only while a film is playing" and asks the wrong
    // question to find out: it tests whether the course's own code is loaded,
    // which is true for the whole of a ride INCLUDING its opening movie. So
    // every verdict raised during that movie is refused -- measured on a real
    // session, seven of them, four camera cuts then two more then an isolated
    // authored step, none held.
    //
    // Whether that is a fault depends on something no amount of reading can
    // settle. The same session reported the opening as smooth with all seven
    // refused, and each hold costs several blocking round trips to the GPU on
    // frames that are already the heaviest of their scene. Granting them could
    // as easily spoil a sequence that currently looks right. So it is a switch,
    // and the comparison is made by looking at the two.
    std::atomic<bool> g_hold_in_course = { false };
}

static inline uint32_t read_u32(uint8_t* rdram, uint32_t addr) {
    // Same addressing the generated code uses for 32-bit loads.
    return *reinterpret_cast<uint32_t*>(rdram + (addr - 0x80000000u));
}

extern "C" void dmaLoadOverlay(uint8_t* rdram, recomp_context* ctx) {
    snap::g_rdram = rdram;
    snap::apply_game_settings(rdram);
    // An overlay load is a safe moment for the settings mailbox: the menu
    // cannot be open while code is being swapped. The menu's strings are
    // staged by the dmaReadVPK0 wrapper below, once their font is in RDRAM.
    snap::stage_menu_assets(rdram);
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
            snap::g_app_level_resident.store(true, std::memory_order_relaxed);
        }
        else if ((vram_start < LevelCodeEnd) && ((vram_start + size) > LevelCodeStart)) {
            snap::g_app_level_resident.store(false, std::memory_order_relaxed);
        }
    }
}

// The game decompresses its VPK0 segments through dmaReadVPK0(rom, ram)
// (the decomp's src/sys/dma.c), synchronously: when the real function
// returns, the whole segment is in RDRAM. The main menu's segment (ROM
// 0xA0F830, decompressed to 0x802B5000 immediately before the main-menu
// overlay is loaded -- app_render/46270.c, start_scene_manager) carries the
// Options screen's pre-rendered text, which is the port's menu font. This is
// the one moment it is certain to be resident and the menu code has not yet
// run, so the font is harvested and the pages' strings staged right here.
// The intro's segment (ROM 0xAA0B80) lands at the same VRAM and does not
// match; the harvest itself reads RDRAM and writes nothing to it.
extern "C" void dmaReadVPK0(uint8_t* rdram, recomp_context* ctx) {
    // a0/a1 before the call: the real function's callees clobber them.
    const uint32_t rom = static_cast<uint32_t>(ctx->r4);
    const uint32_t ram = static_cast<uint32_t>(ctx->r5);
    __real_dmaReadVPK0(rdram, ctx);
    if ((rom == snap::kMainMenuVpk0Rom) && (ram == snap::kMainMenuVpk0Vram)) {
        snap::g_rdram = rdram;
        snap::stage_menu_strings(rdram);
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
// patched AI_LEN read (vram 0x800219D8, RecompiledFuncs/funcs_48.c) consumes.
// The word lives in the port's mailbox page at 0x80C00040 (byte map in
// patches/src/graphics_menu_patch.c). It used to sit at 0x80700004, inside
// the Expansion Pak range 0x80400000-0x807FFFF0 that the game's Snap Station
// boot sweeps with a read-back memory test (func_8009B2BC): a word rewritten
// every frame in that range fails the test, and the kiosk's photo display
// never starts. Nothing of the port's may live in that range. Lives here
// because the MEM_W macro requires a variable literally named rdram.
extern "C" void snap_publish_ai_len(uint8_t* rdram) {
    if (rdram == nullptr) return;
    // ultramodern already models AI_LEN: queued bytes minus a lookahead margin.
    MEM_W(0, (gpr)(int32_t)0x80C00040) = ultramodern::get_remaining_audio_bytes();
}



