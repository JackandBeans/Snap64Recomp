/**
 * @file save_file.cpp
 * @brief Which save file the game plays (settings.h save_file).
 *
 * The runtime opens saves/pokemonsnap.bin as it starts, before the game
 * runs, and can swap to another file at any time through
 * ultramodern::change_save_file, which waits for its saving thread and
 * reads the new file in. The game reads its save once, in its own flash
 * set-up (func_800C0B48_5D9E8, the decompilation's more_funcs/5D500.c,
 * the function that calls osFlashInit), so the swap is made just before
 * that, from the port's wrapper of it (tools/hook_funcs.py lists it): File 2
 * plays saves/pokemonsnap-2.bin, and so on, each a file of its own that
 * starts empty. File 1 is the file the port always had, untouched.
 */
#include <cstdint>
#include <cstdio>
#include <string>

#include "recomp.h"
#include "ultramodern/ultramodern.hpp"

#include "settings.h"

extern "C" {
#include "funcs.h"
}

extern "C" void func_800C0B48_5D9E8(uint8_t* rdram, recomp_context* ctx) {
    static bool chosen = false;
    if (!chosen) {
        chosen = true;
        const int file = snap::settings().save_file;
        if ((file >= 2) && (file <= 4)) {
            const std::u8string name = u8"pokemonsnap-" + std::u8string(1, char8_t('0' + file));
            ultramodern::change_save_file(u8"", name);
            std::printf("[SNAP-SAVE] playing save file %d (saves/pokemonsnap-%d.bin)\n", file, file);
        } else {
            std::printf("[SNAP-SAVE] playing save file 1 (saves/pokemonsnap.bin)\n");
        }
        std::fflush(stdout);
    }
    __real_func_800C0B48_5D9E8(rdram, ctx);
}
