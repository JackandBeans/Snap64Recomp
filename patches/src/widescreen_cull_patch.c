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
/* Host-owned: one while the headset shows the world (src/vr_openxr.cpp). */
#define SNAP_VR_WORLD (*(volatile u8*) 0x80C000E0)
/* Host-owned: how much wider than the cartridge's own margins a Pokemon may
 * be and still be drawn while the headset shows the world, Q8 (256 = the
 * cartridge's). A headset sees about fifty degrees each way against the
 * cartridge's forty-six horizontally and thirty-eight vertically, and the head
 * can pitch past the limit the course sets the camera, so a little over twice
 * the margins covers what can be seen. It must stay BOUNDED: drawing the whole
 * course at once overran the game's own display list buffer, which is sized
 * for what the console drew, and writing past it corrupted the game's memory
 * and wedged it. */
#define SNAP_VR_MARGIN_Q8 (*(volatile u32*) 0x80C000E4)
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

/**
 * Replaces func_80364718_504B28 from src/app_level/5047F0.c -- the per-frame
 * verdict on a Pokemon: the test above on its collision point, with the
 * result kept in the Pokemon's flag 0x100, which is what its render function
 * consults before drawing it (pokemon_detect.c, renderPokemonModelType*),
 * and returned to the photo data builder (app_render/47380.c), which fills
 * the frame's list of photographable Pokemon from it.
 *
 * With the headset showing the world, the head looks wherever it likes while
 * the game's camera points where the photo will be taken, so a Pokemon
 * outside the camera's picture must still draw: the flag is left clear for
 * every Pokemon within the game's own distance. The verdict returned is the
 * cartridge's, so the photo's list, its twelve slots and the scoring behind
 * them see exactly what the console's camera saw.
 */
/* The same test as above with the margins scaled, for the DRAWING verdict
 * while the headset shows the world. Bounded at four times, and the distance
 * test above it is the cartridge's. */
static s32 snapVrOffScreen(f32 x, f32 y, f32 z) {
    f32 outX;
    f32 outY;
    f32 outZ;
    s32 temp;
    s32 xBound;
    s32 yBound;
    u32 q8;
    u32 wide;

    guMtxXFMF(D_803B14D8_5518E8, x, y, z, &outX, &outY, &outZ);
    if (outZ > -1.0f) {
        return 1;
    }
    if (outZ < -10000.0f) {
        return 1;
    }

    q8 = SNAP_VR_MARGIN_Q8;
    if (q8 < 256) {
        q8 = 256;
    }
    if (q8 > 1024) {
        q8 = 1024;
    }
    xBound = (s32) ((240 * q8 + 128) >> 8);
    yBound = (s32) ((180 * q8 + 128) >> 8);

    /* Widescreen widens the picture as well; both apply. */
    wide = SNAP_VIEW_WIDE_Q8;
    if (wide > 256) {
        if (wide > 1024) {
            wide = 1024;
        }
        xBound = (s32) ((xBound * wide + 128) >> 8);
    }

    temp = (outX * 228.506134f) / outZ;
    if (temp < -xBound || temp > xBound) {
        return 1;
    }
    temp = (outY * 228.506134f) / outZ;
    if (temp < -yBound || temp > yBound) {
        return 1;
    }
    return 0;
}

s32 func_80364718_504B28(GObj* obj) {
    Pokemon* pokemon = GET_POKEMON(obj);
    s32 culled;
    s32 hidden;

    if (pokemon->flags & POKEMON_FLAG_40) {
        Pokemon_SetFlag100(obj, false);
        return 0;
    }
    if (10000.0f < pokemon->playerDist) {
        Pokemon_SetFlag100(obj, true);
        return 1;
    }
    culled = func_80364618_504A28(obj, pokemon->collPosition.x, pokemon->collPosition.y, pokemon->collPosition.z);
    if (culled != 0) {
        hidden = true;
        if (SNAP_VR_WORLD) {
            hidden = snapVrOffScreen(pokemon->collPosition.x, pokemon->collPosition.y, pokemon->collPosition.z);
        }
        Pokemon_SetFlag100(obj, hidden);
        return 1;
    }
    Pokemon_SetFlag100(obj, false);
    return 0;
}
