/**
 * Game-side patch: how far ahead a course's Pokemon are created.
 *
 * The world is a chain of blocks and the game keeps a window of them alive
 * around the cart. Entering a block creates the contents of the one after it,
 * so a Pokemon comes into existence exactly one block ahead of where the
 * player is. On the console that was almost always far enough, because a
 * block ahead is usually behind a rock wall, a bend, or a wall of trees.
 *
 * The beach is the exception, and it is the first course the player rides.
 * Its sightlines are open all the way down the shore, so the block boundary
 * where Butterfree and Snorlax are created sits in plain view and they are
 * watched appearing out of nothing. Nothing in the port causes this -- the
 * timing is the game's own -- but the port shows it more plainly than the
 * console did, and it reads as a bug whatever its provenance.
 *
 * Widening the window by one block moves that moment out of sight without
 * changing anything about when the game thinks the world exists: the same
 * blocks, in the same order, by the same callback, each created exactly once.
 * Two functions decide the window, and each gets one more step:
 *
 *   - the initial fill, which populated the block the ride starts in, the one
 *     behind it and the one ahead, now also fills the one after that;
 *   - the per-boundary step, which created the block one ahead of the block
 *     just entered, now creates the one after that instead.
 *
 * Each block is still added exactly once, because the initial fill covers the
 * two ahead and every boundary afterwards adds precisely the one that just
 * came into range. Deletion is untouched, so the live window grows by a
 * single block.
 */

#include "common.h"
#include "world/world.h"

/* Both live in src/world/block.c; the patch links against them there. */
extern BlockFunc2 WorldBlockAddCb;
extern WorldBlock* CurrentWorldBlock;
extern void func_800E2280_5FA30(WorldBlock*);

void func_800E23A8_5FB58(WorldBlock* arg0) {
    s32 i;

    if (arg0 != NULL) {
        WorldBlock* next = arg0;
        for (i = 0; i < 2; i++) {
            next = next->next;
            if (next == NULL) {
                return;
            }
        }
        if (WorldBlockAddCb != NULL) {
            WorldBlockAddCb(next, arg0);
        }
    }
}

WorldBlock* func_800E25E4_5FD94(WorldBlock* arg0) {
    s32 i;
    WorldBlock* v1;

    if (arg0 == NULL) {
        return NULL;
    }

    CurrentWorldBlock = arg0;
    func_800E2280_5FA30(arg0);
    if (WorldBlockAddCb != NULL) {
        WorldBlockAddCb(arg0, arg0);
    }

    for (i = -1; i >= -1; i--) {
        if (arg0->prev == NULL) {
            break;
        }
        v1 = arg0->prev;
        if (WorldBlockAddCb != NULL) {
            WorldBlockAddCb(v1, arg0);
        }
    }

    v1 = arg0;
    for (i = 1; i <= 2; i++) {
        if (v1->next == NULL) {
            break;
        }
        v1 = v1->next;
        if (WorldBlockAddCb != NULL) {
            WorldBlockAddCb(v1, arg0);
        }
    }

    return arg0;
}
