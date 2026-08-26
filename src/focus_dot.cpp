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
    // KSEG0 only: the MEM_ macros subtract 0x80000000 without masking, so a
    // KSEG1 pointer that passes a masked range test is then read far outside
    // what the runtime committed. See the note in matrix_tags.cpp.
    return ((address >> 29) == 4u) && ((address & 0x1FFFFFFFu) < 0x00800000u);
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
    // drew last time, so the test would keep seeing its own output.
    //
    // The original value goes back afterwards. This used to claim the write
    // was free because RDRAM is never presented, and that was wrong in the
    // configuration the game ships in: with render to RAM on, the renderer
    // hashes the framebuffer every frame to notice when the game has drawn
    // into it behind the renderer's back, and a single changed pixel is
    // indistinguishable from the game having done exactly that. It answered
    // by uploading the whole framebuffer and then blocking the game thread
    // on a GPU fence -- every frame, for a pixel the port itself wrote and
    // nobody ever displayed. Leaving RDRAM exactly as the game left it costs
    // one extra load and makes the frame honest.
    const uint32_t centerAddress = framebuffer + snap::DotCenterOffset;
    const uint16_t originalPixel = static_cast<uint16_t>(MEM_H(0, (gpr)(int32_t)centerAddress) & 0xFFFF);

    // The restore is tied to leaving this function rather than to reaching the
    // end of it. The real call waits on a message queue, and ultramodern turns
    // a thread being destroyed while it waits into an exception -- so a scene
    // torn down at the wrong moment unwound straight past the restore and left
    // the sentinel in the framebuffer permanently. The framebuffer hash then
    // differed on every frame afterwards, which is the whole-buffer upload and
    // GPU fence this design exists to avoid.
    struct SentinelGuard {
        uint8_t* rdram;
        uint32_t address;
        uint16_t original;
        bool restore = true;

        ~SentinelGuard() {
            if (restore) {
                uint8_t* rdram = this->rdram;
                MEM_H(0, (gpr)(int32_t)address) = static_cast<int16_t>(original);
            }
        }
    } guard{ rdram, centerAddress, originalPixel };

    MEM_H(0, (gpr)(int32_t)centerAddress) = static_cast<int16_t>(~snap::DotColor);

    __real_PokemonDetector_PostProcessImage(rdram, ctx);

    // MEM_H applies the byte-order XOR itself, so doing it here as well cancels
    // it and samples the pixel next door.
    const bool drewDot = (MEM_H(0, (gpr)(int32_t)centerAddress) & 0xFFFF) == snap::DotColor;
    snap::g_focus_dot_visible = drewDot;

    // When the game DID write here, what is there is already what the game put
    // there and must be left alone.
    guard.restore = !drewDot;
}
