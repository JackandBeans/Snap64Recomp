/**
 * Replaces func_800E2C60_A0A1F0 and func_800E2DC8_A0A358 from
 * src/main_menu/A08E30.c -- the title screen's two movers. One walks
 * Pikachu across the screen from right to left, half a pixel a frame; the
 * other drives the ZERO-ONE, with Todd in it, the same way at a pixel a
 * frame. Each cycles its sprite's animation frames as it goes, and stops
 * once the sprite's x reaches -32:
 *
 *     while (sobj->sprite.x > -32) { step left; ohWait(1); }
 *     hide every frame of the sprite;
 *
 * At x = -32 neither sprite is past the edge yet: the ZERO-ONE is 60 pixels
 * wide and Pikachu 45, so at the stop the back of the vehicle (up to 20
 * columns of it) and the back of Pikachu (up to 11) are still on screen,
 * and they vanish in one frame. Overscan Crop hides all but a few of those columns;
 * with it off, the port shows the cut as the console drew it.
 *
 * Mailbox byte 0x80C00015, the Cutscene Fix that the intro patches also
 * read (beach_intro_patch.c), chooses. It is read once, as each mover
 * starts. 0, the default, stops at -32 exactly as the ROM does. 1 runs each
 * mover on until its sprite is wholly past the left edge: x reaches -60 for
 * the ZERO-ONE and -45 for Pikachu, each sprite's own width. Every frame up
 * to the ROM's stop is the same in both modes, and nothing else on the
 * title waits on the movers.
 */

#include "common.h"

/* The title overlay's show/hide helper: 0 hides the sprite, 1 shows it. */
void func_800E18AC_A08E3C(SObj* sobj, u8 show);

/* The port's settings mailbox at 0x80C00000; this byte is Cutscene Fix,
 * seeded by the port from its saved settings on every overlay load. */
#define SNAP_CUTSCENE_FIX (*(volatile u8*) 0x80C00015)

/* Where each mover stops: the ROM's -32, or the sprite's width. */
#define ROM_END_X       (-32)
#define PIKACHU_WIDTH   45
#define ZERO_ONE_WIDTH  60

void func_800E2C60_A0A1F0(GObj* gobj) {
    SObj* sobj;
    f32 x;
    u8 var_s1;
    u8 var_s2;
    s32 endX;

    /* Read once, here, so a setting changed while the title is up cannot
     * move the stop halfway through the walk. */
    endX = (SNAP_CUTSCENE_FIX != 0) ? -PIKACHU_WIDTH : ROM_END_X;

    x = 287.0f;
    sobj = gobj->data.sobj;
    var_s2 = 0;
    var_s1 = 0;
    sobj->sprite.x = x;

    while (true) {
        var_s2++;
        if ((var_s2 % 4) == 0) {
            func_800E18AC_A08E3C(sobj, false);
            if (var_s1 != 3) {
                sobj->next->sprite.x = sobj->sprite.x;
                sobj = sobj->next;
            } else {
                gobj->data.sobj->sprite.x = sobj->sprite.x;
                sobj = gobj->data.sobj;
            }
            func_800E18AC_A08E3C(sobj, true);
            var_s1++;
            var_s1 %= 4;
        }

        if (sobj->sprite.x > endX) {
            x -= 0.5f;
            sobj->sprite.x = x;
            ohWait(1);
        } else {
            break;
        }
    }

    sobj = gobj->data.sobj;
    while (sobj != NULL) {
        func_800E18AC_A08E3C(sobj, false);
        sobj = sobj->next;
    }

    while (true) {
        ohWait(1);
    }
}

void func_800E2DC8_A0A358(GObj* gobj) {
    SObj* sobj;
    u8 var_s1;
    u8 var_s2;
    s32 endX;

    /* Read once, as above. */
    endX = (SNAP_CUTSCENE_FIX != 0) ? -ZERO_ONE_WIDTH : ROM_END_X;

    sobj = gobj->data.sobj;
    var_s2 = 0;
    var_s1 = 0;
    sobj->sprite.x = 671;

    while (true) {
        var_s2++;
        if ((var_s2 % 4) == 0) {
            func_800E18AC_A08E3C(sobj, false);
            if (var_s1 != 2) {
                sobj->next->sprite.x = sobj->sprite.x;
                sobj = sobj->next;
            } else {
                gobj->data.sobj->sprite.x = sobj->sprite.x;
                sobj = gobj->data.sobj;
            }
            func_800E18AC_A08E3C(sobj, true);
            var_s1++;
            var_s1 %= 3;
        }

        if (sobj->sprite.x > endX) {
            sobj->sprite.x--;
            ohWait(1);
        } else {
            break;
        }
    }

    sobj = gobj->data.sobj;
    while (sobj != NULL) {
        func_800E18AC_A08E3C(sobj, false);
        sobj = sobj->next;
    }

    while (true) {
        ohWait(1);
    }
}
