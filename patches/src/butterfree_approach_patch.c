/**
 * Replaces func_beach_802C7C7C from src/beach/55FB80.c -- the Beach
 * Butterfree's placement proc.
 *
 * The original teleports each Butterfree to a random point of its authored
 * flight path on its first tick and ends:
 *
 *     Pokemon_ResetPathPos(obj);
 *     Pokemon_FollowPath(obj, randFloat(), 0, 0.1f, 0, UPDATE_TARGET_POS);
 *     pokemon->pathProc = NULL; ... omEndProcess(NULL);
 *
 * Block-table Butterfree spawn when the ride crosses a block boundary, in
 * view, mid-animation -- the famous cave-trio pop. With the Spawn Fade
 * setting on, this version gives them an approach instead: the same random
 * path point is chosen, then the Butterfree starts twelve hundred units up
 * in the sky and glides down to it over about a second, fluttering all the
 * way, while the fade brings its body in. It reads as one flying down to
 * join the course rather than materialising inside it.
 *
 * The glide only moves the transform the original proc already positioned,
 * the path parameter and every flag mutation are stock, and with the
 * setting off the function is behaviourally identical to the original.
 * The setting travels through the graphics mailbox byte the port seeds at
 * boot and the Graphics page edits live (0x80C00014).
 */

#include "common.h"
#include "beach/beach.h"
#include "app_level/app_level.h"

#define SNAP_MAILBOX_SPAWN_FADE (*(volatile u8*) 0x80C00014)

void func_beach_802C7C7C(GObj* obj) {
    Pokemon* pokemon = GET_POKEMON(obj);
    Mtx3Float* pos;
    f32 targetY;
    f32 offset;

    Pokemon_ResetPathPos(obj);
    Pokemon_FollowPath(obj, randFloat(), 0, 0.1f, 0.0f, MOVEMENT_FLAG_UPDATE_TARGET_POS);

    if (SNAP_MAILBOX_SPAWN_FADE) {
        // High enough to sit above the view frustum at the distances these
        // spawn at, so the arrival STARTS off screen: the Butterfree dives
        // into frame like it flew in from the sky, rather than appearing in
        // it. The exponential ease means a fast entry and a soft landing on
        // the authored flight path, fluttering the whole way down.
        pos = &GET_TRANSFORM(obj->data.dobj)->pos;
        targetY = pos->v.y;
        offset = 2600.0f;
        while (offset > 10.0f) {
            pos->v.y = targetY + offset;
            offset *= 0.955f;
            ohWait(1);
        }
        pos->v.y = targetY;
    }

    pokemon->pathProc = NULL;
    pokemon->processFlags |= POKEMON_PROCESS_FLAG_PATH_ENDED;
    omEndProcess(NULL);
}
