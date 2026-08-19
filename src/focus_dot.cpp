/**
 * @file focus_dot.cpp
 * @brief Mirrors the camera's focus indicator onto the presented frame.
 *
 * The game draws the indicator itself, in PokemonDetector_PostProcessImage
 * (src/app_level/pokemon_detect.c): once the frame's render task completes it
 * analyses the result and, if a Pokemon is in focus, writes a five by five
 * red dot straight into the framebuffer in RDRAM. On hardware the video
 * interface scans that framebuffer, so the dot appears. RT64 presents its own
 * render target instead, so the game's write lands somewhere never displayed.
 *
 * Rather than restate the game's condition here -- which means duplicating
 * logic, hardcoding the addresses of its state, and evaluating at a different
 * moment than it does -- let the game decide and observe the decision: run the
 * original, then look at the framebuffer it was handed. If the dot is there,
 * the game drew it, and RT64 is asked to draw it too on the next frame it
 * assembles (see State::snapFocusDotRequest).
 *
 * That keeps this correct through anything that changes when the game shows
 * the indicator, because the answer always comes from the game itself.
 */

#include <cstdint>
#include <cstdio>

#include "recomp.h"

extern "C" {
#include "funcs.h"
}

namespace snap {

// Set by the hook below, consumed by send_dl on the next display list.
bool g_focus_dot_visible = false;

namespace {

// GPACK_RGBA5551(255, 0, 0, 255): the colour pokemon_detect.c fills with.
constexpr uint16_t DotColor = 0xF801;

// The dot spans (157..161, 117..121) minus its corners, so this is its centre
// and is always written whenever the dot is drawn at all.
constexpr uint32_t ScreenWidth = 320;
constexpr uint32_t DotCenterX = 157 + 2;
constexpr uint32_t DotCenterY = 117 + 2;
constexpr uint32_t DotCenterOffset = ((DotCenterY * ScreenWidth) + DotCenterX) * sizeof(uint16_t);

// Addresses from patches/game_syms.ld. Regions is a flat array of pointers to
// the 8x8x2 buffers gtlMalloc hands out in PokemonDetector_Create; the 0x50
// gap to PokemonDetector_Pokemons makes MAX_POKEMONS 20.
constexpr uint32_t RegionsBase        = 0x803AE578;
constexpr uint32_t NumPokemonsBase    = 0x803AE570;  // s32[2], by context
constexpr uint32_t AnalyzedPhotoId    = 0x803AEF36;  // u16
constexpr uint32_t HasPokemonInFocus  = 0x803AE758;
constexpr uint32_t MaxPokemons        = 20;
constexpr uint32_t RegionPixels       = 8 * 8;

bool valid_ram_address(uint32_t address) {
    const uint32_t offset = address & 0x1FFFFFFFu;
    return (address >= 0x80000000u) && (offset < 0x00800000u);
}

// Reproduces the comparison PokemonDetector_FindPokemonInFocus makes, purely to
// report it. The detector decides what is in the reticle by differencing the
// centre 8x8 pixels of the screen before and after each Pokemon is drawn, and
// calls a Pokemon in focus when at least 32 of those 64 pixels changed. Under
// HLE those snapshots are produced by the RDP into RDRAM and read back by the
// CPU, so the whole mechanism hinges on the readback carrying real pixels. The
// counts say which it is: all zero means the snapshots are identical and the
// readback or the tile copy is not happening, small non-zero means it is
// working but the Pokemon is not covering the centre, and anything at or above
// 32 means detection fired.
void report_detector(uint8_t* rdram) {
    const uint32_t photoId = MEM_H(0, (gpr)(int32_t)AnalyzedPhotoId) & 0x1;
    const int32_t count = MEM_W(0, (gpr)(int32_t)(NumPokemonsBase + (photoId * 4)));
    if ((count <= 1) || (count > (int32_t)MaxPokemons)) {
        return;
    }

    uint32_t best = 0;
    for (int32_t i = 1; i < count; i++) {
        const uint32_t prevPtr = MEM_W(0, (gpr)(int32_t)(RegionsBase + ((i - 1) * 4)));
        const uint32_t curPtr  = MEM_W(0, (gpr)(int32_t)(RegionsBase + (i * 4)));
        if (!valid_ram_address(prevPtr) || !valid_ram_address(curPtr)) {
            continue;
        }

        uint32_t differing = 0;
        for (uint32_t j = 0; j < RegionPixels; j++) {
            const uint16_t a = MEM_H(0, (gpr)(int32_t)(prevPtr + (j * 2))) & 0xFFFF;
            const uint16_t b = MEM_H(0, (gpr)(int32_t)(curPtr + (j * 2))) & 0xFFFF;
            if (a != b) {
                differing++;
            }
        }

        if (differing > best) {
            best = differing;
        }
    }

    static uint32_t peak = 0;
    static uint32_t calls = 0;
    calls++;
    if ((best > peak) || ((calls % 60) == 0)) {
        if (best > peak) {
            peak = best;
        }
        const uint32_t inFocus = MEM_W(0, (gpr)(int32_t)HasPokemonInFocus);
        printf("[SNAP-FOCUS] regions %d  best diff %u/64 (need 32)  peak %u  inFocus %u  dot %u\n",
               count, best, peak, inFocus, snap::g_focus_dot_visible ? 1u : 0u);
        fflush(stdout);
    }
}

} // namespace
} // namespace snap

// a0 is the framebuffer the scheduler hands the post-process callback.
extern "C" void PokemonDetector_PostProcessImage(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t framebuffer = static_cast<uint32_t>(ctx->r4);

    if (!snap::valid_ram_address(framebuffer)) {
        __real_PokemonDetector_PostProcessImage(rdram, ctx);
        return;
    }

    // Clear the pixel that is about to be sampled, so that finding the dot
    // colour there afterwards can only mean the game wrote it on this call.
    // Reading it cold is not enough: render to RAM copies each rendered frame
    // back over the framebuffer in RDRAM, and that frame contains the dot RT64
    // drew last time, so the test would keep seeing its own output. Writing
    // here is free because RDRAM framebuffer contents are never presented.
    const uint32_t centerAddress = framebuffer + snap::DotCenterOffset;
    MEM_H(0, (gpr)(int32_t)centerAddress) = static_cast<int16_t>(~snap::DotColor);

    __real_PokemonDetector_PostProcessImage(rdram, ctx);

    // MEM_H applies the byte-order XOR itself, so doing it here as well cancels
    // it and samples the pixel next door.
    snap::g_focus_dot_visible = (MEM_H(0, (gpr)(int32_t)centerAddress) & 0xFFFF) == snap::DotColor;

    snap::report_detector(rdram);
}
