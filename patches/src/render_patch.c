/**
 * Game-side patches, compiled and recompiled separately from the game and
 * linked ahead of it so these definitions win over the originals. Nothing in
 * the ROM changes, so section layout, manual_funcs addresses and the overlay
 * table all stay exactly as they are.
 *
 * Every function here replaces a game function of the same name. patch.ld puts
 * their code in .recomp_patch, which is how the recompiler is told so; it
 * rejects a definition that shadows a game function without being marked, and
 * one that is marked but matches nothing.
 *
 * renPrepareModelMatrix is reproduced from src/sys/render.c with one addition:
 * each matrix is preceded by a group naming the OMMtx it came from, which is
 * how RT64 knows which transform is which between frames.
 *
 * The id has to be per matrix rather than per object. A group covers every
 * matrix emitted after it, so tagging once per object leaves the matrices
 * within it to be paired by position in the list, and this loop does not emit
 * a fixed number: it skips by kind, continues early, and defers to custom
 * matrix handlers, all of which vary with animation state. One extra or
 * missing matrix then shifts every pairing after it, and a limb is
 * interpolated towards a different limb -- parts of a model landing elsewhere
 * or collapsing, which is what the lower half of a Pokemon vanishing while
 * the camera pans is. An OMMtx belongs to one matrix of one object and keeps
 * its address for as long as the object exists, so pairing by it cannot
 * shift: a matrix that appears or disappears simply goes unpaired that frame
 * and every other matrix stays correct.
 */

#include "common.h"
#include "sys/om.h"
#include "sys/gtl.h"
#include "sys/mtx.h"
#include "rt64_extended_gbi.h"

/* Neither reserved id can collide with a pointer: an OMMtx is never NULL
   (G_EX_ID_IGNORE) and never 0xFFFFFFFF (G_EX_ID_AUTO). Vertex, tile,
   texture coordinate and lookAt interpolation are all derived from data this
   game rebuilds every frame, so only the transform itself is interpolated. */
/* sinf and cosf are weak aliases of __sinf and __cosf. Calling them by the
   alias emits a relocation named sinf, which the recompiler passes through
   unchanged and the host compiler then takes for its own math intrinsic;
   calling the real symbols instead gets the same rename the recompiled game
   already applies to them. */
#define sinf __sinf
#define cosf __cosf

/* Defined locally in the game's render.c, so it is not in any header; without
   it IDO compiles an implicit call to a function that does not exist. */
#define gSPMvpRecalc(pkt) gImmp21((pkt), G_SPECIAL_1, 0, 1, 0)

/* Defined in the game's render.c; the patch links against them there. */
extern Mtx* renProjectionMatrix;
extern f32 renScaleX;
extern f32 renScaleY;
extern f32 renScaleZ;
extern s32 renIsScaleMtxPushed;
extern Struct_8004B038* renCustomMatrixHandler;
extern Mtx4f renPerspectiveMatrixF;
extern Mtx4f renMvpMatrixF;
extern Mtx4f ren_D_8004AFA8;
extern Mtx4f ren_D_8004AFE8;

/* The identity of a matrix is the serial src/matrix_tags.cpp stamps into it
   when it is handed out, not its address: the allocator is a LIFO free list, so
   an address freed on one line comes back on the next and would otherwise read
   as the object that released it. The serial occupies the next field, which is
   the free list's and is unused for as long as the matrix is allocated. */
#define OM_MTX_TAG(m) (*(u32*) (m))

#define renEXTagModelMatrix(gfx, ommtx)                                            gEXMatrixGroupDecomposed((gfx), OM_MTX_TAG(ommtx), G_EX_NOPUSH, 0,         G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO,                 G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO, G_EX_COMPONENT_SKIP,                 G_EX_COMPONENT_SKIP, G_EX_ORDER_LINEAR, G_EX_EDIT_ALLOW,                       G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP)

/* The billboard kinds do not produce a rigid matrix. The renderer
   reconstructs their overwritten combined matrix as an equivalent world
   transform, and that reconstruction carries projection terms in its
   rotation rows; asking the interpolator to decompose it into translation,
   rotation and scale produces nonsense, and the recomposed result each
   subframe is the streak smeared across the screen. Simple mode interpolates
   the matrix component-wise instead, which for two consecutive billboard
   matrices -- always near each other -- is the correct blend, with no
   decomposition to go wrong. */
#define renEXTagBillboardMatrix(gfx, ommtx)                                        gEXMatrixGroupSimple((gfx), OM_MTX_TAG(ommtx), G_EX_NOPUSH, 0,                 G_EX_COMPONENT_AUTO, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE,   G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_ORDER_LINEAR, G_EX_EDIT_ALLOW,  G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP)

s32 renPrepareModelMatrix(Gfx** gfxPtr, DObj* dobj) {
    Gfx* sp2DC;
    uintptr_t csr;
    s32 sp2D4;
    s32 i;
    f32 f12;
    union Mtx3fi* sp2C8;
    struct Mtx4Float* sp2C4;
    struct Mtx3Float* sp2C0;
    f32 f0;
    s32 sp2B8;
    s32 (*func)(Mtx*, void*, Gfx**);

    sp2DC = *gfxPtr;
    sp2D4 = 0;

    if (dobj->unk_4C != NULL) {
        csr = (uintptr_t) dobj->unk_4C->data;
        for (i = 0; i < 3; i++) {
            switch (dobj->unk_4C->kinds[i]) {
                case 0:
                    break;
                case 1:
                    sp2C8 = (void*) csr;
                    csr += sizeof(union Mtx3fi);
                    break;
                case 2:
                    sp2C4 = (void*) csr;
                    csr += sizeof(struct Mtx4Float);
                    break;
                case 3:
                    sp2C0 = (void*) csr;
                    csr += sizeof(struct Mtx3Float);
                    break;
            }
        }
    }

    for (i = 0; i < dobj->numMatrices; i++) {
        OMMtx* ommtx = dobj->matrices[i];
        if (ommtx != NULL) {
            Mtx** unk;
            Mtx* mtx;

            unk = (Mtx**) &ommtx->unk_08;
            mtx = &ommtx->unk_08;

            if (ommtx->unk_05 != 2) {
                if (ommtx->unk_05 == 4) {
                    if (dobj->obj->lastDrawFrame != (u8) gtlDrawnFrameCounter) {
                        *unk = gtlCurrentGfxHeap.ptr;
                        mtx = gtlCurrentGfxHeap.ptr;
                        gtlCurrentGfxHeap.ptr = (u8*) gtlCurrentGfxHeap.ptr + sizeof(Mtx);
                    } else {
                        switch (ommtx->kind) {
                            case MTX_TYPE_33:
                            case MTX_TYPE_34:
                            case MTX_TYPE_35:
                            case MTX_TYPE_36:
                            case MTX_TYPE_37:
                            case MTX_TYPE_38:
                            case MTX_TYPE_39:
                            case MTX_TYPE_40:
                            case MTX_TYPE_41:
                            case MTX_TYPE_42:
                            case MTX_TYPE_43:
                            case MTX_TYPE_44:
                            case MTX_TYPE_45:
                            case MTX_TYPE_46:
                            case MTX_TYPE_47:
                            case MTX_TYPE_48:
                            case MTX_TYPE_49:
                            case MTX_TYPE_50:
                                mtx = gtlCurrentGfxHeap.ptr;
                                gtlCurrentGfxHeap.ptr = (u8*) gtlCurrentGfxHeap.ptr + sizeof(Mtx);
                                break;
                            default:
                                if (ommtx->kind >= MTX_TYPE_66) {
                                    mtx = gtlCurrentGfxHeap.ptr;
                                    gtlCurrentGfxHeap.ptr = (u8*) gtlCurrentGfxHeap.ptr + sizeof(Mtx);
                                } else {
                                    mtx = *unk;
                                    goto END2;
                                }
                                break;
                        }
                    }
                } else {
                    if (gtlContextId > 0) {
                        mtx = gtlCurrentGfxHeap.ptr;
                        gtlCurrentGfxHeap.ptr = (u8*) gtlCurrentGfxHeap.ptr + sizeof(Mtx);
                    } else if (dobj->obj->lastDrawFrame == (u8) gtlDrawnFrameCounter) {
                        switch (ommtx->kind) {
                            case MTX_TYPE_33:
                            case MTX_TYPE_34:
                            case MTX_TYPE_35:
                            case MTX_TYPE_36:
                            case MTX_TYPE_37:
                            case MTX_TYPE_38:
                            case MTX_TYPE_39:
                            case MTX_TYPE_40:
                            case MTX_TYPE_41:
                            case MTX_TYPE_42:
                            case MTX_TYPE_43:
                            case MTX_TYPE_44:
                            case MTX_TYPE_45:
                            case MTX_TYPE_46:
                            case MTX_TYPE_47:
                            case MTX_TYPE_48:
                            case MTX_TYPE_49:
                            case MTX_TYPE_50:
                                mtx = gtlCurrentGfxHeap.ptr;
                                gtlCurrentGfxHeap.ptr = (u8*) gtlCurrentGfxHeap.ptr + sizeof(Mtx);
                                break;
                            default:
                                if (ommtx->kind >= MTX_TYPE_66) {
                                    mtx = gtlCurrentGfxHeap.ptr;
                                    gtlCurrentGfxHeap.ptr = (u8*) gtlCurrentGfxHeap.ptr + sizeof(Mtx);
                                } else {
                                    if (ommtx->unk_05 != 3) {
                                        goto END2;
                                    }
                                    mtx = gtlCurrentGfxHeap.ptr;
                                    gtlCurrentGfxHeap.ptr = (u8*) gtlCurrentGfxHeap.ptr + sizeof(Mtx);
                                }
                                break;
                        }
                    }
                }

                sp2B8 = 0;
                switch (ommtx->kind) {
                    case MTX_TYPE_1:
                        break;
                    case MTX_TYPE_2:
                        break;
                    case MTX_TYPE_TRANSLATE:
                        hal_translate(mtx, dobj->position.v.x, dobj->position.v.y, dobj->position.v.z);
                        break;
                    case MTX_TYPE_ROTATE_DEG:
                        hal_rotate_deg(mtx, dobj->rotation.a, dobj->rotation.v.x, dobj->rotation.v.y,
                                       dobj->rotation.v.z);
                        break;
                    case MTX_TYPE_ROTATE_DEG_TRANSLATE:
                        hal_rotate_translate_deg(mtx, dobj->position.v.x, dobj->position.v.y, dobj->position.v.z,
                                                 dobj->rotation.a, dobj->rotation.v.x, dobj->rotation.v.y,
                                                 dobj->rotation.v.z);
                        break;
                    case MTX_TYPE_ROTATE_RPY_DEG:
                        hal_rotate_rpy_deg(mtx, dobj->rotation.v.x, dobj->rotation.v.y, dobj->rotation.v.z);
                        break;
                    case MTX_TYPE_ROTATE_RPY_TRANSLATE_DEG:
                        hal_rotate_rpy_translate_deg(mtx, dobj->position.v.x, dobj->position.v.y, dobj->position.v.z,
                                                     dobj->rotation.v.x, dobj->rotation.v.y, dobj->rotation.v.z);
                        break;
                    case MTX_TYPE_ROTATE:
                        hal_rotate(mtx, dobj->rotation.a, dobj->rotation.v.x, dobj->rotation.v.y,
                                   dobj->rotation.v.z);
                        break;
                    case MTX_TYPE_ROTATE_TRANSLATE:
                        hal_rotate_translate(mtx, dobj->position.v.x, dobj->position.v.y, dobj->position.v.z,
                                             dobj->rotation.a, dobj->rotation.v.x, dobj->rotation.v.y,
                                             dobj->rotation.v.z);
                        break;
                    case MTX_TYPE_ROTATE_TRANSLATE_SCALE:
                        hal_rotate_translate_scale(mtx, dobj->position.v.x, dobj->position.v.y, dobj->position.v.z,
                                                   dobj->rotation.a, dobj->rotation.v.x, dobj->rotation.v.y,
                                                   dobj->rotation.v.z, dobj->scale.v.x, dobj->scale.v.y,
                                                   dobj->scale.v.z);
                        renScaleX *= dobj->scale.v.x;
                        break;
                    case MTX_TYPE_ROTATE_RPY:
                        hal_rotate_rpy(mtx, dobj->rotation.v.x, dobj->rotation.v.y, dobj->rotation.v.z);
                        break;
                    case MTX_TYPE_ROTATE_RPY_TRANSLATE:
                        hal_rotate_rpy_translate(mtx, dobj->position.v.x, dobj->position.v.y, dobj->position.v.z,
                                                 dobj->rotation.v.x, dobj->rotation.v.y, dobj->rotation.v.z);
                        break;
                    case MTX_TYPE_ROTATE_RPY_TRANSLATE_SCALE:
                        hal_rotate_rpy_translate_scale(mtx, dobj->position.v.x, dobj->position.v.y, dobj->position.v.z,
                                                       dobj->rotation.v.x, dobj->rotation.v.y, dobj->rotation.v.z,
                                                       dobj->scale.v.x, dobj->scale.v.y, dobj->scale.v.z);
                        renScaleX *= dobj->scale.v.x;
                        break;
                    case MTX_TYPE_ROTATE_PYR:
                        hal_rotate_pyr(mtx, dobj->rotation.v.x, dobj->rotation.v.y, dobj->rotation.v.z);
                        break;
                    case MTX_TYPE_ROTATE_PYR_TRANSLATE:
                        hal_rotate_pyr_translate(mtx, dobj->position.v.x, dobj->position.v.y, dobj->position.v.z,
                                                 dobj->rotation.v.x, dobj->rotation.v.y, dobj->rotation.v.z);
                        break;
                    case MTX_TYPE_ROTATE_PYR_TRANSLATE_SCALE:
                        hal_rotate_pyr_translate_scale(mtx, dobj->position.v.x, dobj->position.v.y, dobj->position.v.z,
                                                       dobj->rotation.v.x, dobj->rotation.v.y, dobj->rotation.v.z,
                                                       dobj->scale.v.x, dobj->scale.v.y, dobj->scale.v.z);
                        renScaleX *= dobj->scale.v.x;
                        break;
                    case MTX_TYPE_SCALE:
                        hal_scale(mtx, dobj->scale.v.x, dobj->scale.v.y, dobj->scale.v.z);
                        renScaleX *= dobj->scale.v.x;
                        break;
                    case MTX_TYPE_33:
                        ren_func_80011608(mtx, dobj, false);
                        break;
                    case MTX_TYPE_34:
                        ren_func_80011608(mtx, dobj, true);
                        break;
                    case MTX_TYPE_35:
                        ren_func_80011268(mtx, dobj, false);
                        break;
                    case MTX_TYPE_36:
                        ren_func_80011268(mtx, dobj, true);
                        break;
                    case MTX_TYPE_37:
                        func_8001174C(mtx, dobj, false);
                        break;
                    case MTX_TYPE_38:
                        func_8001174C(mtx, dobj, true);
                        break;
                    case MTX_TYPE_39:
                        ren_func_80011438(mtx, dobj, false);
                        break;
                    case MTX_TYPE_40:
                        ren_func_80011438(mtx, dobj, true);
                        break;
                    case MTX_TYPE_56:
                        hal_translate(mtx, sp2C8->f.v.x, sp2C8->f.v.y, sp2C8->f.v.z);
                        break;
                    case MTX_TYPE_57:
                        hal_rotate(mtx, sp2C4->a, sp2C4->v.x, sp2C4->v.y, sp2C4->v.z);
                        break;
                    case MTX_TYPE_58:
                        hal_rotate_rpy(mtx, sp2C4->v.x, sp2C4->v.y, sp2C4->v.z);
                        break;
                    case MTX_TYPE_59:
                        hal_scale(mtx, sp2C0->v.x, sp2C0->v.y, sp2C0->v.z);
                        renScaleX *= sp2C0->v.x;
                        renScaleY *= sp2C0->v.y;
                        renScaleZ *= sp2C0->v.z;
                        break;
                    case MTX_TYPE_60:
                        hal_rotate_translate(mtx, sp2C8->f.v.x, sp2C8->f.v.y, sp2C8->f.v.z, sp2C4->a, sp2C4->v.x,
                                             sp2C4->v.y, sp2C4->v.z);
                        break;
                    case MTX_TYPE_61:
                        hal_rotate_translate_scale(mtx, sp2C8->f.v.x, sp2C8->f.v.y, sp2C8->f.v.z, sp2C4->a,
                                                   sp2C4->v.x, sp2C4->v.y, sp2C4->v.z, sp2C0->v.x, sp2C0->v.y,
                                                   sp2C0->v.z);
                        renScaleX *= sp2C0->v.x;
                        renScaleY *= sp2C0->v.y;
                        renScaleZ *= sp2C0->v.z;
                        break;
                    case MTX_TYPE_62:
                        hal_rotate_rpy_translate(mtx, sp2C8->f.v.x, sp2C8->f.v.y, sp2C8->f.v.z, sp2C4->v.x,
                                                 sp2C4->v.y, sp2C4->v.z);
                        break;
                    case MTX_TYPE_63:
                        hal_rotate_rpy_translate_scale(mtx, sp2C8->f.v.x, sp2C8->f.v.y, sp2C8->f.v.z, sp2C4->v.x,
                                                       sp2C4->v.y, sp2C4->v.z, sp2C0->v.x, sp2C0->v.y, sp2C0->v.z);
                        renScaleX *= sp2C0->v.x;
                        renScaleY *= sp2C0->v.y;
                        renScaleZ *= sp2C0->v.z;
                        break;
                    /* The billboard kinds never push a matrix: they recompute
                       the combined matrix and then overwrite its rotation rows
                       one element at a time, and they leave this loop early,
                       before the tag every pushed matrix receives. The
                       renderer reconstructs a world transform for the
                       overwritten matrix and files it under whichever identity
                       was named last -- some other object's -- so between
                       frames the billboard is paired with that object and its
                       corners are interpolated towards it. Snorlax's sleep
                       symbols smear into a streak instead of drawing, and
                       every billboard in the game flashes the same streak for
                       a frame whenever the pairing lands differently. Naming
                       the billboard's own matrix before the overwrite gives
                       the reconstruction the identity it always deserved. */
                    case MTX_TYPE_41:
                        renEXTagBillboardMatrix(sp2DC++, ommtx);
                        gSPMvpRecalc(sp2DC++);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YX_YY_I, renProjectionMatrix->m[0][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YZ_YW_I, renProjectionMatrix->m[0][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZX_ZY_I, renProjectionMatrix->m[0][2]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZZ_ZW_I, renProjectionMatrix->m[0][3]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XX_XY_I, renProjectionMatrix->m[1][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XZ_XW_I, renProjectionMatrix->m[1][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YX_YY_F, renProjectionMatrix->m[2][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YZ_YW_F, renProjectionMatrix->m[2][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZX_ZY_F, renProjectionMatrix->m[2][2]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZZ_ZW_F, renProjectionMatrix->m[2][3]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XX_XY_F, renProjectionMatrix->m[3][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XZ_XW_F, renProjectionMatrix->m[3][1]);
                        continue;
                    case MTX_TYPE_42:
                        renEXTagBillboardMatrix(sp2DC++, ommtx);
                        gSPMvpRecalc(sp2DC++);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XX_XY_I, renProjectionMatrix->m[0][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XZ_XW_I, renProjectionMatrix->m[0][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YX_YY_I, renProjectionMatrix->m[0][2]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YZ_YW_I, renProjectionMatrix->m[0][3]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZX_ZY_I, renProjectionMatrix->m[1][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZZ_ZW_I, renProjectionMatrix->m[1][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XX_XY_F, renProjectionMatrix->m[2][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XZ_XW_F, renProjectionMatrix->m[2][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YX_YY_F, renProjectionMatrix->m[2][2]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YZ_YW_F, renProjectionMatrix->m[2][3]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZX_ZY_F, renProjectionMatrix->m[3][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZZ_ZW_F, renProjectionMatrix->m[3][1]);
                        continue;
                    case MTX_TYPE_43:
                        f12 = dobj->scale.v.y * renScaleX;
                        renScaleX *= dobj->scale.v.x;
                        renMvpMatrixF[0][0] = renPerspectiveMatrixF[0][0] * renScaleX;
                        renMvpMatrixF[0][1] = 0.0f;
                        renMvpMatrixF[0][2] = 0.0f;
                        renMvpMatrixF[0][3] = 0.0f;
                        renMvpMatrixF[1][0] = 0.0f;
                        renMvpMatrixF[1][1] = renPerspectiveMatrixF[1][1] * f12;
                        renMvpMatrixF[1][2] = 0.0f;
                        renMvpMatrixF[1][3] = 0.0f;
                        renMvpMatrixF[2][0] = 0.0f;
                        renMvpMatrixF[2][1] = 0.0f;
                        renMvpMatrixF[2][2] = renPerspectiveMatrixF[2][2] * renScaleX;
                        renMvpMatrixF[2][3] = renPerspectiveMatrixF[2][3] * renScaleX;
                        hal_mtx_f2l(renMvpMatrixF, mtx);
                        renEXTagBillboardMatrix(sp2DC++, ommtx);
                        gSPMvpRecalc(sp2DC++);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YX_YY_I, mtx->m[0][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YZ_YW_I, mtx->m[0][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZX_ZY_I, mtx->m[0][2]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZZ_ZW_I, mtx->m[0][3]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XX_XY_I, mtx->m[1][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XZ_XW_I, mtx->m[1][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YX_YY_F, mtx->m[2][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YZ_YW_F, mtx->m[2][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZX_ZY_F, mtx->m[2][2]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZZ_ZW_F, mtx->m[2][3]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XX_XY_F, mtx->m[3][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XZ_XW_F, mtx->m[3][1]);
                        continue;
                    case MTX_TYPE_44: {
                        f12 = dobj->scale.v.y * renScaleX;
                        renScaleX *= dobj->scale.v.x;
                        renMvpMatrixF[0][0] = renPerspectiveMatrixF[0][0] * renScaleX;
                        renMvpMatrixF[0][1] = 0.0f;
                        renMvpMatrixF[0][2] = 0.0f;
                        renMvpMatrixF[0][3] = 0.0f;
                        renMvpMatrixF[1][0] = 0.0f;
                        renMvpMatrixF[1][1] = renPerspectiveMatrixF[1][1] * f12;
                        renMvpMatrixF[1][2] = 0.0f;
                        renMvpMatrixF[1][3] = 0.0f;
                        renMvpMatrixF[2][0] = 0.0f;
                        renMvpMatrixF[2][1] = 0.0f;
                        renMvpMatrixF[2][2] = renPerspectiveMatrixF[2][2] * renScaleX;
                        renMvpMatrixF[2][3] = renPerspectiveMatrixF[2][3] * renScaleX;
                        hal_mtx_f2l(renMvpMatrixF, mtx);
                        renEXTagBillboardMatrix(sp2DC++, ommtx);
                        gSPMvpRecalc(sp2DC++);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XX_XY_I, mtx->m[0][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XZ_XW_I, mtx->m[0][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YX_YY_I, mtx->m[0][2]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YZ_YW_I, mtx->m[0][3]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZX_ZY_I, mtx->m[1][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZZ_ZW_I, mtx->m[1][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XX_XY_F, mtx->m[2][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XZ_XW_F, mtx->m[2][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YX_YY_F, mtx->m[2][2]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YZ_YW_F, mtx->m[2][3]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZX_ZY_F, mtx->m[3][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZZ_ZW_F, mtx->m[3][1]);
                        continue;
                    }
                    case MTX_TYPE_45: {
                        f32 sp1D4 = sinf(dobj->rotation.v.x);
                        f32 f0 = cosf(dobj->rotation.v.x);

                        f12 = dobj->scale.v.y * renScaleX;
                        renScaleX *= dobj->scale.v.x;
                        renMvpMatrixF[0][0] = renPerspectiveMatrixF[0][0] * renScaleX * f0;
                        renMvpMatrixF[1][0] = renPerspectiveMatrixF[0][0] * renScaleX * -sp1D4;
                        renMvpMatrixF[0][1] = renPerspectiveMatrixF[1][1] * f12 * sp1D4;
                        renMvpMatrixF[1][1] = renPerspectiveMatrixF[1][1] * f12 * f0;
                        renMvpMatrixF[0][2] = 0.0f;
                        renMvpMatrixF[1][2] = 0.0f;
                        renMvpMatrixF[0][3] = 0.0f;
                        renMvpMatrixF[1][3] = 0.0f;
                        renMvpMatrixF[2][0] = 0.0f;
                        renMvpMatrixF[2][1] = 0.0f;
                        renMvpMatrixF[2][2] = renPerspectiveMatrixF[2][2] * renScaleX;
                        renMvpMatrixF[2][3] = renPerspectiveMatrixF[2][3] * renScaleX;
                        hal_mtx_f2l(renMvpMatrixF, mtx);
                        renEXTagBillboardMatrix(sp2DC++, ommtx);
                        gSPMvpRecalc(sp2DC++);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XX_XY_I, mtx->m[0][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XZ_XW_I, mtx->m[0][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YX_YY_I, mtx->m[0][2]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YZ_YW_I, mtx->m[0][3]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZX_ZY_I, mtx->m[1][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZZ_ZW_I, mtx->m[1][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XX_XY_F, mtx->m[2][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XZ_XW_F, mtx->m[2][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YX_YY_F, mtx->m[2][2]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YZ_YW_F, mtx->m[2][3]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZX_ZY_F, mtx->m[3][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZZ_ZW_F, mtx->m[3][1]);
                        continue;
                    }
                    case MTX_TYPE_46: {
                        f32 sp1D4 = sinf(dobj->rotation.v.z);
                        f32 f0 = cosf(dobj->rotation.v.z);

                        f12 = dobj->scale.v.y * renScaleX;
                        renScaleX *= dobj->scale.v.x;

                        renMvpMatrixF[0][0] = renPerspectiveMatrixF[0][0] * renScaleX * f0;
                        renMvpMatrixF[1][0] = renPerspectiveMatrixF[0][0] * renScaleX * -sp1D4;
                        renMvpMatrixF[0][1] = renPerspectiveMatrixF[1][1] * f12 * sp1D4;
                        renMvpMatrixF[1][1] = renPerspectiveMatrixF[1][1] * f12 * f0;
                        renMvpMatrixF[0][2] = 0.0f;
                        renMvpMatrixF[1][2] = 0.0f;
                        renMvpMatrixF[0][3] = 0.0f;
                        renMvpMatrixF[1][3] = 0.0f;
                        renMvpMatrixF[2][0] = 0.0f;
                        renMvpMatrixF[2][1] = 0.0f;
                        renMvpMatrixF[2][2] = renPerspectiveMatrixF[2][2] * renScaleX;
                        renMvpMatrixF[2][3] = renPerspectiveMatrixF[2][3] * renScaleX;
                        hal_mtx_f2l(renMvpMatrixF, mtx);
                        renEXTagBillboardMatrix(sp2DC++, ommtx);
                        gSPMvpRecalc(sp2DC++);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XX_XY_I, mtx->m[0][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XZ_XW_I, mtx->m[0][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YX_YY_I, mtx->m[0][2]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YZ_YW_I, mtx->m[0][3]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZX_ZY_I, mtx->m[1][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZZ_ZW_I, mtx->m[1][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XX_XY_F, mtx->m[2][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XZ_XW_F, mtx->m[2][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YX_YY_F, mtx->m[2][2]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YZ_YW_F, mtx->m[2][3]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZX_ZY_F, mtx->m[3][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZZ_ZW_F, mtx->m[3][1]);
                        continue;
                    }
                    case MTX_TYPE_47: {
                        f12 = dobj->scale.v.y * renScaleX;
                        renScaleX *= dobj->scale.v.x;
                        renMvpMatrixF[0][0] = ren_D_8004AFA8[0][0] * renScaleX;
                        renMvpMatrixF[0][1] = ren_D_8004AFA8[0][1] * renScaleX;
                        renMvpMatrixF[0][2] = ren_D_8004AFA8[0][2] * renScaleX;
                        renMvpMatrixF[0][3] = ren_D_8004AFA8[0][3] * renScaleX;
                        renMvpMatrixF[1][0] = ren_D_8004AFA8[1][0] * f12;
                        renMvpMatrixF[1][1] = ren_D_8004AFA8[1][1] * f12;
                        renMvpMatrixF[1][2] = ren_D_8004AFA8[1][2] * f12;
                        renMvpMatrixF[1][3] = ren_D_8004AFA8[1][3] * f12;
                        renMvpMatrixF[2][0] = ren_D_8004AFA8[2][0] * renScaleX;
                        renMvpMatrixF[2][1] = ren_D_8004AFA8[2][1] * renScaleX;
                        renMvpMatrixF[2][2] = ren_D_8004AFA8[2][2] * renScaleX;
                        renMvpMatrixF[2][3] = ren_D_8004AFA8[2][3] * renScaleX;
                        hal_mtx_f2l(renMvpMatrixF, mtx);
                        renEXTagBillboardMatrix(sp2DC++, ommtx);
                        gSPMvpRecalc(sp2DC++);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YX_YY_I, mtx->m[0][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YZ_YW_I, mtx->m[0][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZX_ZY_I, mtx->m[0][2]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZZ_ZW_I, mtx->m[0][3]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XX_XY_I, mtx->m[1][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XZ_XW_I, mtx->m[1][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YX_YY_F, mtx->m[2][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YZ_YW_F, mtx->m[2][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZX_ZY_F, mtx->m[2][2]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZZ_ZW_F, mtx->m[2][3]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XX_XY_F, mtx->m[3][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XZ_XW_F, mtx->m[3][1]);
                        continue;
                    }
                    case MTX_TYPE_48: {
                        f12 = dobj->scale.v.y * renScaleX;
                        renScaleX *= dobj->scale.v.x;
                        renMvpMatrixF[0][0] = ren_D_8004AFA8[0][0] * renScaleX;
                        renMvpMatrixF[0][1] = ren_D_8004AFA8[0][1] * renScaleX;
                        renMvpMatrixF[0][2] = ren_D_8004AFA8[0][2] * renScaleX;
                        renMvpMatrixF[0][3] = ren_D_8004AFA8[0][3] * renScaleX;
                        renMvpMatrixF[1][0] = ren_D_8004AFA8[1][0] * f12;
                        renMvpMatrixF[1][1] = ren_D_8004AFA8[1][1] * f12;
                        renMvpMatrixF[1][2] = ren_D_8004AFA8[1][2] * f12;
                        renMvpMatrixF[1][3] = ren_D_8004AFA8[1][3] * f12;
                        renMvpMatrixF[2][0] = ren_D_8004AFA8[2][0] * renScaleX;
                        renMvpMatrixF[2][1] = ren_D_8004AFA8[2][1] * renScaleX;
                        renMvpMatrixF[2][2] = ren_D_8004AFA8[2][2] * renScaleX;
                        renMvpMatrixF[2][3] = ren_D_8004AFA8[2][3] * renScaleX;
                        hal_mtx_f2l(renMvpMatrixF, mtx);
                        renEXTagBillboardMatrix(sp2DC++, ommtx);
                        gSPMvpRecalc(sp2DC++);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XX_XY_I, mtx->m[0][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XZ_XW_I, mtx->m[0][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YX_YY_I, mtx->m[0][2]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YZ_YW_I, mtx->m[0][3]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZX_ZY_I, mtx->m[1][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZZ_ZW_I, mtx->m[1][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XX_XY_F, mtx->m[2][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XZ_XW_F, mtx->m[2][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YX_YY_F, mtx->m[2][2]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YZ_YW_F, mtx->m[2][3]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZX_ZY_F, mtx->m[3][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZZ_ZW_F, mtx->m[3][1]);
                        continue;
                    }
                    case MTX_TYPE_49: {
                        f12 = dobj->scale.v.y * renScaleX;
                        renScaleX *= dobj->scale.v.x;
                        renMvpMatrixF[0][0] = ren_D_8004AFE8[0][0] * renScaleX;
                        renMvpMatrixF[0][1] = ren_D_8004AFE8[0][1] * renScaleX;
                        renMvpMatrixF[0][2] = ren_D_8004AFE8[0][2] * renScaleX;
                        renMvpMatrixF[0][3] = ren_D_8004AFE8[0][3] * renScaleX;
                        renMvpMatrixF[1][0] = ren_D_8004AFE8[1][0] * f12;
                        renMvpMatrixF[1][1] = ren_D_8004AFE8[1][1] * f12;
                        renMvpMatrixF[1][2] = ren_D_8004AFE8[1][2] * f12;
                        renMvpMatrixF[1][3] = ren_D_8004AFE8[1][3] * f12;
                        renMvpMatrixF[2][0] = ren_D_8004AFE8[2][0] * renScaleX;
                        renMvpMatrixF[2][1] = ren_D_8004AFE8[2][1] * renScaleX;
                        renMvpMatrixF[2][2] = ren_D_8004AFE8[2][2] * renScaleX;
                        renMvpMatrixF[2][3] = ren_D_8004AFE8[2][3] * renScaleX;
                        hal_mtx_f2l(renMvpMatrixF, mtx);
                        renEXTagBillboardMatrix(sp2DC++, ommtx);
                        gSPMvpRecalc(sp2DC++);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YX_YY_I, mtx->m[0][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YZ_YW_I, mtx->m[0][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZX_ZY_I, mtx->m[0][2]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZZ_ZW_I, mtx->m[0][3]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XX_XY_I, mtx->m[1][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XZ_XW_I, mtx->m[1][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YX_YY_F, mtx->m[2][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YZ_YW_F, mtx->m[2][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZX_ZY_F, mtx->m[2][2]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZZ_ZW_F, mtx->m[2][3]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XX_XY_F, mtx->m[3][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XZ_XW_F, mtx->m[3][1]);
                        continue;
                    }
                    case MTX_TYPE_50:
                        f12 = dobj->scale.v.y * renScaleX;
                        renScaleX *= dobj->scale.v.x;
                        renMvpMatrixF[0][0] = ren_D_8004AFE8[0][0] * renScaleX;
                        renMvpMatrixF[0][1] = ren_D_8004AFE8[0][1] * renScaleX;
                        renMvpMatrixF[0][2] = ren_D_8004AFE8[0][2] * renScaleX;
                        renMvpMatrixF[0][3] = ren_D_8004AFE8[0][3] * renScaleX;
                        renMvpMatrixF[1][0] = ren_D_8004AFE8[1][0] * f12;
                        renMvpMatrixF[1][1] = ren_D_8004AFE8[1][1] * f12;
                        renMvpMatrixF[1][2] = ren_D_8004AFE8[1][2] * f12;
                        renMvpMatrixF[1][3] = ren_D_8004AFE8[1][3] * f12;
                        renMvpMatrixF[2][0] = ren_D_8004AFE8[2][0] * renScaleX;
                        renMvpMatrixF[2][1] = ren_D_8004AFE8[2][1] * renScaleX;
                        renMvpMatrixF[2][2] = ren_D_8004AFE8[2][2] * renScaleX;
                        renMvpMatrixF[2][3] = ren_D_8004AFE8[2][3] * renScaleX;
                        hal_mtx_f2l(renMvpMatrixF, mtx);
                        renEXTagBillboardMatrix(sp2DC++, ommtx);
                        gSPMvpRecalc(sp2DC++);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XX_XY_I, mtx->m[0][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XZ_XW_I, mtx->m[0][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YX_YY_I, mtx->m[0][2]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YZ_YW_I, mtx->m[0][3]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZX_ZY_I, mtx->m[1][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZZ_ZW_I, mtx->m[1][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XX_XY_F, mtx->m[2][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_XZ_XW_F, mtx->m[2][1]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YX_YY_F, mtx->m[2][2]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_YZ_YW_F, mtx->m[2][3]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZX_ZY_F, mtx->m[3][0]);
                        gMoveWd(sp2DC++, G_MW_MATRIX, G_MWO_MATRIX_ZZ_ZW_F, mtx->m[3][1]);
                        continue;
                    case MTX_TYPE_51:
                        hal_rotate_rpy_translate(mtx, dobj->position.v.x * renScaleX, dobj->position.v.y * renScaleY,
                                                 dobj->position.v.z * renScaleZ, dobj->rotation.v.x, dobj->rotation.v.y,
                                                 dobj->rotation.v.z);
                        break;
                    case MTX_TYPE_52:
                        hal_rotate_pyr_translate(mtx, dobj->position.v.x * renScaleX, dobj->position.v.y * renScaleY,
                                                 dobj->position.v.z * renScaleZ, dobj->rotation.v.x, dobj->rotation.v.y,
                                                 dobj->rotation.v.z);
                        break;
                    case MTX_TYPE_53:
                        renScaleX *= dobj->scale.v.x;
                        renScaleY *= dobj->scale.v.y;
                        renScaleZ *= dobj->scale.v.z;
                        hal_scale(mtx, renScaleX, renScaleY, renScaleZ);
                        renIsScaleMtxPushed = true;
                        sp2B8 = 2;
                        break;
                    case MTX_TYPE_55:
                        hal_translate(mtx, dobj->position.v.x * renScaleX, dobj->position.v.y * renScaleY,
                                      dobj->position.v.z * renScaleZ);
                        break;
                    case MTX_TYPE_64:
                        renScaleX *= sp2C0->v.x;
                        renScaleY *= sp2C0->v.y;
                        renScaleZ *= sp2C0->v.z;
                        continue;
                    case MTX_TYPE_65:
                        hal_rotate_translate(mtx, sp2C8->f.v.x, sp2C8->f.v.y, sp2C8->f.v.z, sp2C4->a, sp2C4->v.x,
                                             sp2C4->v.y, sp2C4->v.z);
                        renScaleX *= sp2C0->v.x;
                        renScaleY *= sp2C0->v.y;
                        renScaleZ *= sp2C0->v.z;
                        break;
                    case MTX_TYPE_54:
                        func_8001ECD0(mtx, dobj->position.v.x, dobj->position.v.y, dobj->position.v.z,
                                      dobj->rotation.v.x, dobj->rotation.v.y, dobj->rotation.v.z, renScaleX, renScaleY,
                                      renScaleZ, dobj->scale.v.x, dobj->scale.v.y, dobj->scale.v.z);
                        renScaleX *= dobj->scale.v.x;
                        renScaleY *= dobj->scale.v.y;
                        renScaleZ *= dobj->scale.v.z;
                        break;
                    default:
                        if (ommtx->kind >= MTX_TYPE_66 && renCustomMatrixHandler != NULL) {
                            func = dobj->obj->lastDrawFrame != (u8) gtlDrawnFrameCounter
                                       ? renCustomMatrixHandler[ommtx->kind - MTX_TYPE_66].unk_00
                                       : renCustomMatrixHandler[ommtx->kind - MTX_TYPE_66].unk_04;
                            sp2B8 = func(mtx, dobj, &sp2DC);
                        }
                        if (sp2B8 == 1) {
                            continue;
                        }
                        break;
                }
            END2:
                if (ommtx->unk_05 == 1 && &ommtx->unk_08 == mtx) {
                    ommtx->unk_05 = 2;
                }
            }
            if (ommtx->kind != MTX_TYPE_2) {
                renEXTagModelMatrix(sp2DC++, ommtx);
                if (sp2B8 == 2 || sp2D4 == 0 && ((uintptr_t) dobj->parent == 1 || dobj->next != NULL)) {
                    gSPMatrix(sp2DC++, mtx, G_MTX_PUSH | G_MTX_MUL | G_MTX_MODELVIEW);
                } else {
                    gSPMatrix(sp2DC++, mtx, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_MODELVIEW);
                }
                sp2D4++;
            }
        }
    }

    *gfxPtr = sp2DC;
    return sp2D4;
}

/**
 * The game's other matrix emitter, reproduced from src/sys/render.c with the
 * same addition. Objects reach the display list through one of these two, and
 * only what they tag can be paired between frames, so leaving this one alone
 * interpolated the Pokemon and left the world it stands in at the native
 * rate.
 */
s32 ren_func_80013C5C(Gfx** gfxPtr, DObj* dobj) {
    Gfx* gfxPos = *gfxPtr;
    s32 i;
    s32 mtxCount = 0;

    for (i = 0; i < dobj->numMatrices; i++) {
        OMMtx* ommtx = dobj->matrices[i];
        if (ommtx != NULL) {
            Mtx** unk;
            Mtx* mtx;

            if (ommtx->kind != MTX_TYPE_53) {
                continue;
            }

            unk = (Mtx**) &ommtx->unk_08;
            mtx = &ommtx->unk_08;

            if (ommtx->unk_05 != 2) {
                if (ommtx->unk_05 == 4) {
                    if (dobj->obj->lastDrawFrame != (u8) gtlDrawnFrameCounter) {
                        *unk = gtlCurrentGfxHeap.ptr;
                        mtx = gtlCurrentGfxHeap.ptr;
                        gtlCurrentGfxHeap.ptr = (u8*) gtlCurrentGfxHeap.ptr + sizeof(Mtx);
                    } else {
                        mtx = *unk;
                        goto END;
                    }
                } else {
                    if (gtlContextId > 0) {
                        mtx = gtlCurrentGfxHeap.ptr;
                        gtlCurrentGfxHeap.ptr = (u8*) gtlCurrentGfxHeap.ptr + sizeof(Mtx);
                    } else if (dobj->obj->lastDrawFrame == (u8) gtlDrawnFrameCounter) {
                        if (ommtx->unk_05 != 3) {
                            goto END;
                        }
                        mtx = gtlCurrentGfxHeap.ptr;
                        gtlCurrentGfxHeap.ptr = (u8*) gtlCurrentGfxHeap.ptr + sizeof(Mtx);
                    }
                }
                hal_scale(mtx, renScaleX, renScaleY, renScaleZ);
                renIsScaleMtxPushed = true;
            END:
                if (ommtx->unk_05 == 1 && &ommtx->unk_08 == mtx) {
                    ommtx->unk_05 = 2;
                }
            }

            renEXTagModelMatrix(gfxPos++, ommtx);
            gSPMatrix(gfxPos++, mtx, G_MTX_PUSH | G_MTX_MUL | G_MTX_MODELVIEW);
            mtxCount++;
        }
    }

    *gfxPtr = gfxPos;
    return mtxCount;
}
