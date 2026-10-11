/**
 * @file host_hooks.cpp
 * @brief Mods' hooks on the functions the port itself instruments.
 *
 * The runtime the port inherited gives a hooked function a fresh copy of
 * itself: the function's code recompiled live from the ROM with the hook
 * calls inside, and the function patched to jump to it (lib/N64ModernRuntime,
 * mods.cpp, regenerate_with_hooks). For most of the game that is fine. For a
 * function the port intercepts -- renamed by tools/hook_funcs.py and wrapped
 * in C++ (fx_draw, omGetMtx, fx_createParticle, renPrepareCameraMatrix, ...),
 * or with a call inserted inside it or an instruction rewritten -- the fresh
 * copy has none of the port's work, and the patch lands on the port's own
 * wrapper, so the work is gone for as long as the mod is on.
 *
 * Found on Oct 6 2026 with the Island at Night mod's hook on fx_draw: with
 * the hook, whatever it did and whether its option was on or off, Doduo's
 * dust and every other effect stepped as the view turned, because the names
 * src/fx_tags.cpp gives the particles' rectangles (the inner hook at
 * 0x800A5158) were no longer being written -- the function running was the
 * cartridge's, not the port's.
 *
 * So the port declares those functions to the runtime before the mods load,
 * by the place a mod's hook names (the section's ROM address and the address
 * in RAM, both from src/recomp_overlays.inl), and the runtime keeps them as
 * compiled and records the hook slots instead of regenerating. The generated
 * __real_<name> (RecompiledFuncs/funcs_snap_hooks.c, between the port's
 * <name> and the game's __game_<name>) asks for the slots here and runs them:
 * the entry hooks before the game's code, the return hooks after it, as the
 * regenerated copy would have, with the port's work around them kept.
 */
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <unordered_set>

#include "librecomp/mods.hpp"
#include "snap_host_hooks.h"
#include "settings.h"

namespace snap {

void register_host_hooked_functions() {
    for (size_t i = 0; i < snap_host_hooked_count; i++) {
        const SnapHostHookedFunction& f = snap_host_hooked_functions[i];
        recomp::mods::mark_host_hooked_function(f.section_rom, f.vram);
    }
}

} // namespace snap

namespace {

const char* host_hooked_name(uint32_t section_rom, uint32_t vram) {
    for (size_t i = 0; i < snap_host_hooked_count; i++) {
        const SnapHostHookedFunction& f = snap_host_hooked_functions[i];
        if ((f.section_rom == section_rom) && (f.vram == vram)) {
            return f.name;
        }
    }
    return "?";
}

} // namespace

// The hook slot mods have on the function, or none; pending until the mods
// have loaded, when the caller asks again. Said once in the log per function
// and side when a mod's hook is found there, so a log shows which of the
// port's functions a mod's hooks run through.
extern "C" size_t snap_host_hook_slot(uint32_t section_rom, uint32_t vram, int at_return) {
    const size_t slot = recomp::mods::host_hook_slot(section_rom, vram, at_return != 0);
    if (slot < recomp::mods::host_hook_pending) {
        static std::mutex said_mutex;
        static std::unordered_set<uint64_t> said;
        // The addresses are word aligned, so the low bit is free for the side.
        const uint64_t key = (uint64_t(section_rom) << 32) | uint64_t(vram | (at_return ? 1u : 0u));
        std::lock_guard<std::mutex> lock(said_mutex);
        if (said.insert(key).second) {
            const char* name = host_hooked_name(section_rom, vram);
            printf("[SNAP-MODS] a mod hooks %s at %s: the hook runs from the port's own %s, whose work is kept\n",
                name, at_return ? "return" : "entry", name);
            fflush(stdout);
        }
    }
    return slot;
}

extern "C" void snap_host_hook_run(uint8_t* rdram, recomp_context* ctx, size_t slot) {
    recomp::mods::run_hook(rdram, ctx, slot);
}
