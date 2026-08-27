/**
 * Replaces fillBorderBlack from src/app_level/player.c.
 *
 * When the viewfinder is raised the game letterboxes itself: the main scissor
 * animates to an inset rectangle and this function blacks out everything
 * around it with four fill rectangles. Those rectangles are authored against a
 * 320 wide screen, because that is the only screen the game knows.
 *
 * Under the renderer's widescreen expansion the image is wider than that. The
 * top and bottom bands still stretch across it, but the left and right bands
 * land at the centred 4:3 edges and the widescreen margins outside them are
 * never repainted -- by anything, ever, while the viewfinder is up. The
 * presentation rotates through a pool of render targets, each of which keeps
 * whatever scene was last drawn into its margins at a different moment, so the
 * margins flicker through stale content at the interpolated rate for as long
 * as the viewfinder is raised.
 *
 * The extended commands exist for exactly this. Each band's edges are aligned
 * to the true edges of the image: the full-width bands stretch from the wide
 * left edge to the wide right edge, the left band runs from the wide left edge
 * to its original inner edge, and the right band from its original inner edge
 * to the wide right edge. The scissor is widened around the pass and restored
 * after, since these rectangles are clipped by it. With widescreen off the
 * wide edges are the 4:3 edges and every rectangle lands exactly where the
 * original put it.
 */

#include "common.h"

#include "rt64_extended_gbi.h"

void fillBorderBlack(Gfx** gfxPtr, s32 xmin, s32 ymin, s32 xmax, s32 ymax) {
    Gfx* gfxPos = *gfxPtr;

    gEXPushScissor(gfxPos++);
    gEXSetScissor(gfxPos++, G_SC_NON_INTERLACE, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_RIGHT, 0, 0, 0, 240);

    /* The four bands animate as the viewfinder raises and lowers, and a
     * rectangle without a name cannot be paired with itself in the previous
     * frame, so the bands stepped at the game's rate while the scene behind
     * them glided at the display's. One name per band, self-closing: a band
     * whose rectangle degenerates at the animation's extremes is silently
     * dropped by the renderer, and with one shared group that drop would
     * shift the ordinals and refuse the whole letterbox for the frame --
     * named singly, only the vanished band goes unpaired. */

    /* Top and bottom bands: both edges follow the true edges of the image. */
    gEXSetRectAlign(gfxPos++, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_RIGHT, 0, 0, 0, 0);
    gEXRectGroupOne(gfxPos++, 0x50534C30);  /* 'PSL0' */
    gDPFillRectangle(gfxPos++, 0, 0, 319, ymin);
    gDPPipeSync(gfxPos++);
    gEXRectGroupOne(gfxPos++, 0x50534C31);  /* 'PSL1' */
    gDPFillRectangle(gfxPos++, 0, ymax, 319, 239);
    gDPPipeSync(gfxPos++);

    /* Left band: from the wide left edge to the inset's own edge. */
    gEXSetRectAlign(gfxPos++, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_NONE, 0, 0, 0, 0);
    gEXRectGroupOne(gfxPos++, 0x50534C32);  /* 'PSL2' */
    gDPFillRectangle(gfxPos++, 0, ymin - 1, xmin, ymax);
    gDPPipeSync(gfxPos++);

    /* Right band: from the inset's own edge to the wide right edge. */
    gEXSetRectAlign(gfxPos++, G_EX_ORIGIN_NONE, G_EX_ORIGIN_RIGHT, 0, 0, 0, 0);
    gEXRectGroupOne(gfxPos++, 0x50534C33);  /* 'PSL3' */
    gDPFillRectangle(gfxPos++, xmax, ymin - 1, 319, ymax);
    gDPPipeSync(gfxPos++);

    /* If the last band degenerated, its un-consumed name is still pending and
     * would fall onto whatever the game draws next. Close it outright. */
    gEXRectGroup(gfxPos++, 0);
    gEXSetRectAlign(gfxPos++, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, 0, 0, 0, 0);
    gEXPopScissor(gfxPos++);

    *gfxPtr = gfxPos;
}
