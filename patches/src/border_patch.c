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
 * land at the centered 4:3 edges and the widescreen margins outside them are
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
 *
 * The inset itself is authored on the 4:3 screen too, and that left a jump.
 * Raising the viewfinder moves the inset three pixels a frame for ten frames
 * (the coroutines around setMainCameraViewport in player.c). While it is
 * anything but zero the scene is cropped to the 4:3 inset and the side bands
 * cover the margins beside the 4:3 screen as well; at zero the scene fills
 * the wide picture. So the margin, 53 pixels a side at 16:9, was covered on
 * the first frame of raising the viewfinder and uncovered on the last frame
 * of lowering it, where the rest of the travel is 3 a frame (measured on
 * captured frames, Oct 4 2026: the left band ran 93 .. 86 and then 6).
 *
 * Under widescreen the inset now travels from the picture's true edge: an
 * inset of x on the 4:3 screen sits x * (margin + 30) / 30 from the wide
 * edge, so nothing at zero and the same 4:3 box as before at the full 30.
 * The renderer crops the scene there (rt64_framebuffer_renderer.cpp, where
 * the viewfinder's crop is converted), from the scissor the game still sets
 * in its own 4:3 numbers. The side bands end there, measured from the wide
 * left edge and a pixel further in: the scene is drawn after them and over
 * that pixel, where a band that fell short would leave a column nobody
 * draws. With widescreen off the bands are exactly what they were.
 */

#include "common.h"

#include "rt64_extended_gbi.h"

/* Host-owned: the renderer's horizontal widening, Q8 (256 = none). */
#define SNAP_VIEW_WIDE_Q8 (*(volatile u32*) 0x80C00044)

/* The viewfinder's inset at its fullest, in the game's pixels. */
#define SNAP_FULL_INSET 30

/* The margin beside the 4:3 screen, one side, in quarter pixels; zero with
 * widescreen off (or on a host that never wrote the word). */
static s32 snapWideMargin4(void) {
    u32 q8;

    q8 = SNAP_VIEW_WIDE_Q8;
    if (q8 <= 256) {
        return 0;
    }
    if (q8 > 1024) {
        q8 = 1024;
    }
    return (s32) ((640 * (q8 - 256)) >> 8);
}

/* Where a band over an inset of `inset` pixels on the 4:3 screen ends, in
 * pixels from the picture's true edge: the inset's place, rounded up, and
 * one pixel more. */
static s32 snapWideInset(s32 inset, s32 margin4) {
    s32 part;

    if (inset <= 0) {
        return 0;
    }
    part = (inset < SNAP_FULL_INSET) ? inset : SNAP_FULL_INSET;
    return inset + (part * margin4 + SNAP_FULL_INSET * 4 - 1) / (SNAP_FULL_INSET * 4) + 1;
}

/* The wide picture's width in pixels, rounded down: the right band starts
 * that far from the left edge, less its inset. */
static s32 snapWideWidth(s32 margin4) {
    return 320 + (2 * margin4) / 4;
}

void fillBorderBlack(Gfx** gfxPtr, s32 xmin, s32 ymin, s32 xmax, s32 ymax) {
    Gfx* gfxPos = *gfxPtr;
    s32 margin4;

    margin4 = snapWideMargin4();

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

    if (margin4 == 0) {
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
    } else {
        /* Widescreen: the same two bands, ending where the renderer crops
         * the scene, both measured from the wide left edge. */
        gEXSetRectAlign(gfxPos++, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_LEFT, 0, 0, 0, 0);
        gEXRectGroupOne(gfxPos++, 0x50534C32);  /* 'PSL2' */
        gDPFillRectangle(gfxPos++, 0, ymin - 1, snapWideInset(xmin, margin4), ymax);
        gDPPipeSync(gfxPos++);

        /* The right band runs on past the picture and the scissor ends it:
         * the renderer drops a rectangle whose right is left of its left
         * before it applies any origin, so 319 from the right edge will
         * not do here as it does at 4:3. */
        gEXRectGroupOne(gfxPos++, 0x50534C33);  /* 'PSL3' */
        gDPFillRectangle(gfxPos++, snapWideWidth(margin4) - snapWideInset(320 - xmax, margin4), ymin - 1, 1023, ymax);
        gDPPipeSync(gfxPos++);
    }

    /* If the last band degenerated, its un-consumed name is still pending and
     * would fall onto whatever the game draws next. Close it outright. */
    gEXRectGroup(gfxPos++, 0);
    gEXSetRectAlign(gfxPos++, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, 0, 0, 0, 0);
    gEXPopScissor(gfxPos++);

    *gfxPtr = gfxPos;
}
