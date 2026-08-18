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

bool valid_ram_address(uint32_t address) {
    const uint32_t offset = address & 0x1FFFFFFFu;
    return (address >= 0x80000000u) && (offset < 0x00800000u);
}

} // namespace
} // namespace snap

// a0 is the framebuffer the scheduler hands the post-process callback.
extern "C" void PokemonDetector_PostProcessImage(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t framebuffer = static_cast<uint32_t>(ctx->r4);

    __real_PokemonDetector_PostProcessImage(rdram, ctx);

    if (!snap::valid_ram_address(framebuffer)) {
        return;
    }

    // A 16-bit read lands at addr^2 in the runtime's byte order.
    const uint32_t centerAddress = (framebuffer + snap::DotCenterOffset) ^ 2;
    snap::g_focus_dot_visible = (MEM_H(0, (gpr)(int32_t)centerAddress) & 0xFFFF) == snap::DotColor;
}
