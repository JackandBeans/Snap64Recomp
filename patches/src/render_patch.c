/**
 * Game-side patches, compiled and recompiled separately from the game and
 * linked ahead of it so these definitions win over the originals. Nothing in
 * the ROM changes, so section layout, manual_funcs addresses and the overlay
 * table all stay exactly as they are.
 *
 * renRenderModelTypeACommon is reproduced from src/sys/render.c with one
 * addition: a matrix group naming the DObj whose matrices follow. That is what
 * lets RT64 pair an object with itself between frames instead of guessing
 * identity from geometry, and it is the same thing ports built for RT64 patch
 * into their games.
 *
 * Every function here replaces a game function of the same name. patch.ld puts
 * their code in .recomp_patch, which is how the recompiler is told so; it
 * rejects a definition that shadows a game function without being marked, and
 * one that is marked but matches nothing.
 */

#include "common.h"
#include "sys/om.h"
#include "sys/gtl.h"
#include "sys/mtx.h"
#include "rt64_extended_gbi.h"

extern f32 renScaleX;
extern f32 renScaleY;
extern f32 renScaleZ;
extern s32 renIsScaleMtxPushed;

s32 renPrepareModelMatrix(Gfx** gfxPtr, DObj* dobj);
void renLoadTextures(DObj* dobj, Gfx** gfxPtr);

/**
 * A DObj keeps its address for as long as the object exists, and a group
 * applies to every matrix emitted after it, so one command identifies all of
 * an object's matrices. Neither reserved id can collide with a pointer: a
 * DObj is never NULL (G_EX_ID_IGNORE) and never 0xFFFFFFFF (G_EX_ID_AUTO).
 */
#define renEXTagModelMatrix(gfx, dobj)                                        \
    gEXMatrixGroupDecomposed((gfx), (u32) (uintptr_t) (dobj), G_EX_NOPUSH, 0, \
        G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO,        \
        G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO, G_EX_COMPONENT_SKIP,        \
        G_EX_COMPONENT_SKIP, G_EX_ORDER_LINEAR, G_EX_EDIT_ALLOW,              \
        G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP)

void renRenderModelTypeACommon(GObj* gobj, Gfx** gfxPtr) {
    s32 ret;
    DObj* dobj;

    dobj = gobj->data.dobj;

    renScaleX = renScaleY = renScaleZ = 1.0f;
    renIsScaleMtxPushed = false;

    if (dobj->payload.dlist != NULL) {
        if (dobj->flags == 0) {
            renEXTagModelMatrix((*gfxPtr)++, dobj);
            ret = renPrepareModelMatrix(gfxPtr, dobj);
            renLoadTextures(dobj, gfxPtr);
            gSPDisplayList((*gfxPtr)++, dobj->payload.dlist);

            if (renIsScaleMtxPushed) {
                gSPPopMatrix((*gfxPtr)++, G_MTX_MODELVIEW);
            }
            renIsScaleMtxPushed = false;

            if (ret != 0 && ((uintptr_t) dobj->parent == 1 || dobj->next != NULL)) {
                gSPPopMatrix((*gfxPtr)++, G_MTX_MODELVIEW);
            }
        }
    }
}
