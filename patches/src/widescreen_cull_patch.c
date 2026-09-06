/**
 * Replaces func_80364618_504A28 from src/app_level/5047F0.c -- the test that
 * decides whether a Pokemon is on screen.
 *
 * The game transforms the Pokemon's collision point into camera space and
 * projects it at a fixed focal length of 228.506 pixels (160 * cot 35
 * degrees: a 70-degree horizontal field over 320 pixels), then rejects it
 * outside +/-240 pixels horizontally or +/-180 vertically -- 1.5 times the
 * 4:3 half-screen each way, a margin so a Pokemon whose centre has left
 * the picture still draws while its body is in it. It never reads the
 * camera's aspect: the bounds are the cartridge's own picture, in pixels.
 *
 * The port's Widescreen draws a wider picture (the renderer widens the
 * projection by max(window width/height, 4/3) / (4/3) and nothing in the
 * game changes), so the test keeps culling at the 4:3 margin while the
 * picture reaches further: at 16:9 the visible half-angle is 42.8 degrees
 * against the 46.4-degree cull, so near, large Pokemon pop out while still
 * partly on screen; at 21:9 the picture (50.5 degrees) reaches past the
 * cull and fully visible Pokemon vanish.
 *
 * The fix scales the horizontal bound by the same factor the renderer is
 * applying, which the port publishes in the mailbox word at 0x80C00044 in
 * Q8 (256 = none), every tick, from the window it actually draws into.
 * The vertical bound stays: Widescreen never changes the vertical field.
 * 256 or 0 (a host that never wrote the word) leaves the test exactly as
 * the cartridge has it; garbage is clamped to four times.
 *
 * The same test serves the photo's object list (func_803647BC_504BCC for
 * items, func_80364718_504B28 for Pokemon), which accepts at most twelve
 * entries; a wider bound admits edge Pokemon into that list too. Effect
 * sprites have a test of their own in fx_draw and still pop at the 4:3
 * edge; that is a separate patch.
 */

#include "common.h"
#include "app_level/app_level.h"

/* The view matrix the game refreshes each frame from the main camera
 * (func_803643E0_5047F0); patches/game_syms.ld carries its address. */
extern Mtx4f D_803B14D8_5518E8;

/* Host-owned: the renderer's horizontal widening, Q8 (256 = none). */
#define SNAP_VIEW_WIDE_Q8 (*(volatile u32*) 0x80C00044)
/* Host-owned, zeroed at the seed: how many verdicts the widened bound has
 * turned from culled to drawn, so a replay can measure the patch without
 * a picture (the host prints it under SNAP_STATS). */
#define SNAP_VIEW_WIDE_SAVED (*(volatile u32*) 0x80C00048)

s32 func_80364618_504A28(GObj* obj, f32 x, f32 y, f32 z) {
    f32 outX;
    f32 outY;
    f32 outZ;
    s32 temp;
    s32 xBound;
    u32 q8;

    guMtxXFMF(D_803B14D8_5518E8, x, y, z, &outX, &outY, &outZ);
    if (outZ > -1.0f) {
        return 1;
    }
    if (outZ < -10000.0f) {
        return 1;
    }
    xBound = 240;
    q8 = SNAP_VIEW_WIDE_Q8;
    if (q8 > 256) {
        if (q8 > 1024) {
            q8 = 1024;
        }
        xBound = (s32) ((240 * q8 + 128) >> 8);
    }
    temp = (outX * 228.506134f) / outZ;
    if (temp < -xBound || temp > xBound) {
        return 1;
    }
    if (temp < -240 || temp > 240) {
        SNAP_VIEW_WIDE_SAVED = SNAP_VIEW_WIDE_SAVED + 1;
    }
    temp = (outY * 228.506134f) / outZ;
    if (temp < -180 || temp > 180) {
        return 1;
    }
    return 0;
}
