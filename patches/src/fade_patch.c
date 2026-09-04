/**
 * @file fade_patch.c
 * @brief The screen fade covers the whole frame at the port's resolution.
 *
 * The fade to black between screens (decomp src/app_render/52DE0.c) is a
 * quad of +-50 units in the plane x = 0, scaled 3.2 wide by 2.4 tall, seen
 * by a camera at x = 290 with a 45 degree vertical field of view. At that
 * distance the screen spans +-120.1 units tall and +-160.2 wide, and the
 * quad reaches +-120 by +-160: an eighth of a pixel short at every edge.
 * The console's RDP samples pixel centres, so that margin never showed on
 * a console; the port renders at many times the resolution and samples
 * sub-pixel centres inside the margin, and a bright hairline of the scene
 * stayed along the top, left and right of every fade, brighter the more
 * the screen behind it was lit.
 *
 * The two render functions that draw this quad full-screen widen its scale
 * to 3.3 by 2.5 -- five pixels past every edge, entirely off screen --
 * before the model matrix is built. The colour, alpha and pacing of the
 * fade are untouched; the other quad drawn with this display list, the
 * iris that shrinks to a point, keeps its own scale.
 */
#include "common.h"

extern Gfx D_800AECB0[];
extern s32 D_800AF054;
extern s32 D_800AF058;
extern s32 D_800AF05C;
extern s32 D_800AF060;

/* The full-screen quad is the one created at 3.2 by 2.4; anything else
 * drawn through these functions is left alone. */
static void fadeCoverFrame(DObj* dobj) {
    if (dobj->scale.v.y == 3.2f && dobj->scale.v.z == 2.4f) {
        dobj->scale.v.y = 3.3f;
        dobj->scale.v.z = 2.5f;
    }
}

void func_800A750C(GObj* gobj) {
    Gfx* gfx;
    s32 sp30;

    fadeCoverFrame(gobj->data.dobj);
    gfx = gMainGfxPos[1];
    gDPPipeSync(gfx++);
    sp30 = renPrepareModelMatrix(&gfx, gobj->data.dobj);
    renLoadTextures(gobj->data.dobj, &gfx);

    gDPSetPrimColor(gfx++, 0, 0, D_800AF054, D_800AF058, D_800AF05C, D_800AF060);
    gDPSetRenderMode(gfx++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gSPDisplayList(gfx++, D_800AECB0);

    if (sp30 != 0 && (gobj->data.dobj->parent == (void*) 1 || gobj->data.dobj->next != NULL)) {
        gSPPopMatrix(gfx++, G_MTX_MODELVIEW);
    }
    gMainGfxPos[1] = gfx;
}

void func_800A7A58(GObj* gobj) {
    Gfx* gfx;
    s32 sp30;

    fadeCoverFrame(gobj->data.dobj);
    gfx = gMainGfxPos[1];
    gDPPipeSync(gfx++);
    sp30 = renPrepareModelMatrix(&gfx, gobj->data.dobj);
    renLoadTextures(gobj->data.dobj, &gfx);

    gDPSetPrimColor(gfx++, 0, 0, D_800AF054, D_800AF058, D_800AF05C, D_800AF060);
    gDPSetRenderMode(gfx++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    gSPDisplayList(gfx++, D_800AECB0);

    if (sp30 != 0 && (gobj->data.dobj->parent == (void*) 1 || gobj->data.dobj->next != NULL)) {
        gSPPopMatrix(gfx++, G_MTX_MODELVIEW);
    }
    gMainGfxPos[1] = gfx;
}
