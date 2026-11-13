/**
 * Replaces func_80364718_504B28 from src/app_level/5047F0.c -- the call,
 * once per Pokemon per frame, that decides both whether the Pokemon is drawn
 * and whether the frame's photo list may hold it.
 *
 * Its test, func_80364618_504A28, transforms the Pokemon's collision point
 * into camera space and projects it at a fixed focal length of 228.506
 * pixels (160 * cot 35 degrees: a 70-degree horizontal field over 320
 * pixels), then rejects it outside +/-240 pixels horizontally or +/-180
 * vertically -- 1.5 times the 4:3 half-screen each way, a margin so a
 * Pokemon whose center has left the picture still draws while its body is
 * in it. It never reads the camera's aspect: the bounds are the cartridge's
 * own picture, in pixels.
 *
 * The verdict goes two ways. The call sets the Pokemon's flag 0x100, which
 * the seven render wrappers (pokemon_detect.c) read to skip the Pokemon, and
 * returns it to func_8009BF48, which leaves an off-screen Pokemon out of the
 * frame's photo list (func_8009C25C, twelve entries at most, in list order).
 * A photo taken that frame keeps that list.
 *
 * The port's Widescreen draws a wider picture (the renderer widens the
 * projection by max(window width/height, 4/3) / (4/3) and nothing in the
 * game changes), so the cartridge's bound would cull while the picture
 * reaches further: at 16:9 the visible half-angle is 42.8 degrees against
 * the 46.4-degree cull, so near, large Pokemon pop out while still partly
 * on screen; at 21:9 the picture (50.5 degrees) reaches past the cull and
 * fully visible Pokemon vanish.
 *
 * So the flag takes a bound scaled by the same factor the renderer applies,
 * which the port publishes in the mailbox word at 0x80C00044 in Q8 (256 =
 * none), every tick, from the window it actually draws into. The vertical
 * bound stays: Widescreen never changes the vertical field. The return value
 * stays the cartridge's own test, unchanged, so the photo list holds what the
 * cartridge would list: until Nov 2026 the widened test answered both, and a
 * wide window put edge Pokemon in photos (on the Beach tape, one with no
 * points and no pixels). Items (func_803647BC_504BCC) call the cartridge's
 * test only for their list, so they keep its bound too. 256 or 0 (a host that
 * never wrote the word) leaves the call exactly as the cartridge has it;
 * garbage is clamped to four times. Effect sprites have a test of their own
 * in fx_draw; the host widens that one on the recompiled code
 * (src/fx_tags.cpp, snap_fx_x_translate), so they reach the wide edge as well.
 */

#include "common.h"
#include "app_level/app_level.h"

/* The view matrix the game refreshes each frame from the main camera
 * (func_803643E0_5047F0); patches/game_syms.ld carries its address. */
extern Mtx4f D_803B14D8_5518E8;

/* The cartridge's own test (src/app_level/5047F0.c), not replaced. */
s32 func_80364618_504A28(GObj* obj, f32 x, f32 y, f32 z);

/* Host-owned: the renderer's horizontal widening, Q8 (256 = none). */
#define SNAP_VIEW_WIDE_Q8 (*(volatile u32*) 0x80C00044)
/* Host-owned, zeroed at the seed: how many Pokemon the widened bound has
 * drawn that the cartridge's would have culled, a frame each, so a replay
 * can measure the patch without a picture (the host prints it under
 * SNAP_STATS). */
#define SNAP_VIEW_WIDE_SAVED (*(volatile u32*) 0x80C00048)

/* The cartridge's test with the horizontal bound scaled by q8 (> 256):
 * 1 off screen, 0 on. */
static s32 snap_off_screen_wide(f32 x, f32 y, f32 z, u32 q8) {
    f32 outX;
    f32 outY;
    f32 outZ;
    s32 temp;
    s32 xBound;

    if (q8 > 1024) {
        q8 = 1024;
    }
    xBound = (s32) ((240 * q8 + 128) >> 8);
    guMtxXFMF(D_803B14D8_5518E8, x, y, z, &outX, &outY, &outZ);
    if (outZ > -1.0f) {
        return 1;
    }
    if (outZ < -10000.0f) {
        return 1;
    }
    temp = (outX * 228.506134f) / outZ;
    if (temp < -xBound || temp > xBound) {
        return 1;
    }
    temp = (outY * 228.506134f) / outZ;
    if (temp < -180 || temp > 180) {
        return 1;
    }
    return 0;
}

s32 func_80364718_504B28(GObj* obj) {
    Pokemon* pokemon = GET_POKEMON(obj);
    s32 offScreen;
    s32 drawnOffScreen;
    u32 q8;

    if (GET_POKEMON(obj)->flags & POKEMON_FLAG_40) {
        Pokemon_SetFlag100(obj, false);
        return 0;
    }
    if (10000.0f < GET_POKEMON(obj)->playerDist) {
        Pokemon_SetFlag100(obj, true);
        return 1;
    }
    offScreen = func_80364618_504A28(obj, pokemon->collPosition.x, pokemon->collPosition.y, pokemon->collPosition.z);
    drawnOffScreen = offScreen;
    q8 = SNAP_VIEW_WIDE_Q8;
    if ((offScreen != 0) && (q8 > 256)) {
        drawnOffScreen = snap_off_screen_wide(pokemon->collPosition.x, pokemon->collPosition.y, pokemon->collPosition.z, q8);
        if (drawnOffScreen == 0) {
            SNAP_VIEW_WIDE_SAVED = SNAP_VIEW_WIDE_SAVED + 1;
        }
    }
    Pokemon_SetFlag100(obj, (drawnOffScreen != 0) ? true : false);
    return offScreen;
}
