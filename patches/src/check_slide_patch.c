/**
 * Replaces func_801E1FA8_991A18 and func_801DDCF8_98D768 from
 * src/photo_check/98C330.c -- the Professor's check: the picture being
 * judged slides in from the right, and the one already in the report from
 * the left to be compared with it.
 *
 * Both slides are an ease kept in a float: each step closes a tenth, or
 * twelve hundredths, of what is left. The sprite takes the whole pixels of
 * it, so near its end the picture moves a pixel, waits a step or three, and
 * moves another. At the game's own rate nobody can tell. Drawn between the
 * frames, each of those pixels is a small glide and each wait a stop: the
 * slide came in smoothly and ended in steps (seen by hand, Oct 4 2026).
 *
 * These are the ROM's functions with one addition. Each time a slide places
 * its sprite, what the float holds past the whole pixel is left for the port
 * in quarter pixels, with the sprite it belongs to (the two mailbox words
 * below). The port's sprite hook (src/rect_tags.cpp) draws that picture so
 * much further along while the frame rate is raised. The sprite, and
 * everything the game does with it, is what it was.
 */

#include "common.h"
#include "window/window.h"
#include "photo_check/photo_check.h"

typedef struct ObjPair {
    /* 0x00 */ GObj* gobj;
    /* 0x04 */ SObj* sobj;
} ObjPair; // size = 0x8

extern s32 D_801F3E2C_9A389C;
extern s32 D_801F3E30_9A38A0;
extern s32 D_801F3E34_9A38A4;
extern ObjPair D_802290A0_9D8B10[6];
extern s32 D_8022918C_9D8BFC;

s32 func_801DD1A8_98CC18(s32 arg0);
s32 func_801DE204_98DC74(Photo* photo);

/* The port's mailbox at 0x80C00000: two words, one per slide, each naming a
 * sprite object in its low 24 bits and, above them, four more than the
 * quarter pixels its picture stands past the sprite's whole pixel (-3..3);
 * zero is no slide. The patch's own; the host clears both at a scene's
 * set-up. */
#define SNAP_FINE_SLOT(i) (*(volatile u32*) (0x80C000E0 + (i) * 4))

static void snapFineSet(s32 slot, SObj* sobj, f32 x) {
    s32 quarters;

    quarters = (s32) (x * 4.0f) - sobj->sprite.x * 4;
    if (quarters < -3 || quarters > 3) {
        SNAP_FINE_SLOT(slot) = 0;
        return;
    }
    SNAP_FINE_SLOT(slot) = ((u32) (quarters + 4) << 24) | ((u32) sobj & 0x00FFFFFF);
}

void func_801DDCF8_98D768(GObj* arg0) {
    SObj* sp3C;
    f32 sp38;
    f32 sp34;
    s32 i;
    s32 sp2C;

    auPlaySoundWithParams(0x4F, 0x7FFF, 0x2C, 1.65f, 0xA);
    D_8022918C_9D8BFC = false;
    sp2C = (s32) arg0->userData;
    sp3C = D_802290A0_9D8B10[1].gobj->data.sobj;
    sp3C->sprite.x = -140;
    sp3C->sprite.y = 37;
    sp3C->sprite.attr &= ~SP_HIDDEN;
    SNAP_FINE_SLOT(1) = 0;
    i = 30;

    for (sp34 = 0.0f, sp38 = -199.0f; sp38 > -200.0f; ohWait(1)) {
        if (D_801F3E2C_9A389C == 2) {
            sp3C->sprite.x = -140;
            SNAP_FINE_SLOT(1) = 0;
            break;
        }

        if (D_801F3E34_9A38A4) {
            sp3C->sprite.x = 59;
            SNAP_FINE_SLOT(1) = 0;
            sp38 = 0.0f;
            sp34 = 0.0f;
        } else if (D_801F3E2C_9A389C != 0) {
            if (i > 0) {
                i--;
                continue;
            } else {
                if (!D_8022918C_9D8BFC) {
                    auPlaySoundWithParams(0x50, 0x7FFF, 0x22, 1.0f, 0xA);
                    D_8022918C_9D8BFC = true;
                }
                sp34 = -210.0f;
            }
        }

        sp38 += (sp34 - sp38) * 0.1f;
        if (D_801F3E2C_9A389C == 3) {
            i = 10;

            for (i--; i > 0; i--) {
                if (i % 2) {
                    sp3C->sprite.attr |= SP_HIDDEN;
                } else {
                    sp3C->sprite.attr &= ~SP_HIDDEN;
                }
                ohWait(2);
            }
            break;
        } else {
            if (ABS(sp38) < 0.5) {
                sp3C->sprite.x = 59;
                SNAP_FINE_SLOT(1) = 0;
            } else {
                sp3C->sprite.x = 59.0f + sp38;
                snapFineSet(1, sp3C, 59.0f + sp38);
            }
        }
    }

    sp3C->sprite.attr |= SP_HIDDEN;
    SNAP_FINE_SLOT(1) = 0;
    D_801F3E2C_9A389C = 2;
    omDeleteGObj(NULL);
    ohWait(0x63);
}

s32 func_801E1FA8_991A18(Photo* arg0, s32 arg1, s32 arg2) {
    SObj* sp5C;
    f32 sp58;
    f32 sp54;
    s32 button;
    ObjPair* sp4C;
    s32 sp48;
    s32 i;
    s32 sp40;
    s32 sp3C;

    D_801F3E34_9A38A4 = false;
    if (func_800BF3D4_5C274(arg0->pkmnID)) {
        sp5C = D_802290A0_9D8B10[1].gobj->data.sobj;
        sp5C->sprite.x = -140;
        sp5C->sprite.y = 37;
        sp5C->sprite.attr &= ~SP_HIDDEN;
        func_80374714_847EC4(func_800BF710_5C5B0(arg0->pkmnID), &D_802290A0_9D8B10[1].sobj->sprite);
        ohWait(2);
        func_80374714_847EC4(func_800BF710_5C5B0(arg0->pkmnID), &D_802290A0_9D8B10[1].sobj->sprite);
        ohWait(2);
    }
    auPlaySoundWithParams(0x4F, 0x7FFF, 0x4A, 1.0f, 0xA);
    sp4C = D_802290A0_9D8B10;
    sp5C = D_802290A0_9D8B10[0].gobj->data.sobj;
    sp5C->sprite.x = 431;
    sp5C->sprite.y = 37;
    sp5C->sprite.attr &= ~SP_HIDDEN;
    SNAP_FINE_SLOT(0) = 0;
    func_80374714_847EC4(arg0->unk_0, &D_802290A0_9D8B10[0].sobj->sprite);
    ohWait(2);
    func_80374714_847EC4(arg0->unk_0, &D_802290A0_9D8B10[0].sobj->sprite);
    if (arg1 != 0) {
        sp58 = -239.0f;
    } else {
        sp58 = 240.0f;
    }
    sp5C->sprite.x = 471;
    sp5C->sprite.attr &= ~SP_HIDDEN;
    if (func_800BF3D4_5C274(arg0->pkmnID)) {
        sp48 = 0xE7;
        sp3C = 1;
    } else {
        sp48 = 0xE7;
        sp3C = 0;
    }

    sp40 = false;
    for (sp54 = 0.0f; sp58 > -240.0f;) {
        button = func_801DD1A8_98CC18(1);
        if (!sp40 && button != 0) {
            if (button == B_BUTTON) {
                D_801F3E34_9A38A4 = true;
            }
            sp58 = 0.0f;
            sp54 = 0.0f;
        }
        sp58 += (sp54 - sp58) * 0.12;
        sp5C->sprite.x = sp48 + sp58 + arg2;
        snapFineSet(0, sp5C, sp48 + sp58 + arg2);

        if (ABS(sp54 - sp58) < 0.1) {
            UIText_SetPrintDelay(2);
            func_8036EB80_842330(1);
            sp40 = true;
            i = func_801DE204_98DC74(arg0);
            if (D_801F3E2C_9A389C == 1) {
                i = 10;

                for (i--; i > 0; i--) {
                    if (i % 2) {
                        sp5C->sprite.attr |= SP_HIDDEN;
                    } else {
                        sp5C->sprite.attr &= ~SP_HIDDEN;
                    }
                    ohWait(2);
                }
            }
            D_801F3E30_9A38A0 = 1;

            for (i--; i > 0; i--) {
                ohWait(1);
            }
            UIText_SetPrintDelay(0);
            func_8036EB80_842330(0);
            sp54 = -250.0f;
        }
    }
    sp5C->sprite.attr |= SP_HIDDEN;
    SNAP_FINE_SLOT(0) = 0;
    return 0;
}
