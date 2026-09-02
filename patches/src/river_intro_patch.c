/**
 * Replaces func_802E22E4_6CA0C4 from src/river/6C9EB0.c -- the River course
 * intro's hand-off to first person.
 *
 * This is the Beach intro's script with River's symbols (see
 * beach_intro_patch.c for the full account): a 290-tick wait on the camera
 * animation, then an eleven-step ease from the cinematic pose back to the
 * first-person eye, waiting a tick after each, and only THEN the hand-off
 * that resets the camera and deletes the ZERO-ONE model:
 *
 *     for (i = 0; i <= 10; i++) { pose(i); ohWait(1); }
 *     func_802E2194_6C9F74();              // reset camera, delete the model
 *
 * So River has the same off-by-one: at i == 10 the camera sits at the eye
 * position for a full frame with Todd's model still drawn, and i == 9 is one
 * step short of it. The console drew both.
 *
 * Mailbox byte 0x80C00015, read once when the intro starts, chooses. 0, the
 * default, runs the loop and the hand-off exactly as the ROM does. 1 draws
 * the dive through i == 8, then lands the final pose and the hand-off in one
 * tick, so the model is gone before the camera is anywhere it could cut into
 * it. Every other frame, the skip path, the timeout and the sounds are the
 * same in both modes.
 *
 * River's hand-off stops two sounds where Beach's stops one. That function is
 * the ROM's own and is not replaced; only the glide is.
 */

#include "common.h"
#include "river/river.h"
#include "app_level/app_level.h"

extern AnimCmd* D_8014A660_2BA730;
extern AnimCmd** D_8014B450_2BB520;
extern AnimCmd D_8014BF30_2BC000;
extern s32 D_802E4B80_6CC960;

void func_802E2120_6C9F00(DObj* dobj, s32 arg1, f32 arg2);
void func_802E2194_6C9F74(void);
void func_802E222C_6CA00C(GObj* obj);

/* The port's settings mailbox at 0x80C00000; this byte is the hand-off
 * choice, seeded by the port from its saved settings. */
#define SNAP_INTRO_HANDOFF_FIX (*(volatile u8*) 0x80C00015)

void func_802E22E4_6CA0C4(GObj* obj) {
    s32 unused[4];
    GObj* gobj;
    f32 baseAtX, baseAtY;
    f32 startEyeX, startEyeY, startEyeZ;
    f32 startAtX, startAtY, startAtZ;
    f32 baseEyeX, baseEyeY, baseEyeZ;
    OMCamera* cam;
    GObjProcess* proc;
    f32 baseAtZ;
    s32 i;
    s32 lastPose;

    /* Read once, here, so a setting changed during the intro cannot switch
     * the glide's mode halfway through it. 10 is the ROM's loop bound. */
    lastPose = (SNAP_INTRO_HANDOFF_FIX != 0) ? 8 : 10;

    cam = getMainCamera();
    gobj = getMainCamera()->obj;

    startEyeX = cam->viewMtx.lookAt.eye.x;
    startEyeY = cam->viewMtx.lookAt.eye.y;
    startEyeZ = cam->viewMtx.lookAt.eye.z;
    startAtX = cam->viewMtx.lookAt.at.x;
    startAtY = cam->viewMtx.lookAt.at.y;
    startAtZ = cam->viewMtx.lookAt.at.z;

    cam->animSpeed = 0.5f;
    animSetCameraAnimation(cam, &D_8014BF30_2BC000, 0.0f);
    proc = omCreateProcess(gobj, animUpdateCameraAnimation, 1, 1);
    PlayerModel_SetAnimation(&D_8014A660_2BA730, &D_8014B450_2BB520, 0.0f, 0.5f);
    D_802E4B80_6CC960 = 0;
    obj->fnAnimCallback = func_802E2120_6C9F00;
    omCreateProcess(obj, func_802E222C_6CA00C, 0, 1);

    i = 0;
    while (D_802E4B80_6CC960 == 0 && i < 290) {
        if (gContInputPressedButtons & (A_BUTTON | START_BUTTON)) {
            omEndProcess(proc);
            func_802E2194_6C9F74();
        }
        ohWait(1);
        i++;
    }

    omEndProcess(proc);

    baseEyeX = cam->viewMtx.lookAt.eye.x;
    baseEyeY = cam->viewMtx.lookAt.eye.y;
    baseEyeZ = cam->viewMtx.lookAt.eye.z;
    baseAtX = cam->viewMtx.lookAt.at.x;
    baseAtY = cam->viewMtx.lookAt.at.y;
    baseAtZ = cam->viewMtx.lookAt.at.z;

    /* The ROM's ease, verbatim, up to lastPose. Its last two poses are the
     * degenerate ones: i == 10 IS the first-person eye position -- drawn
     * from inside the back of Todd's head -- and i == 9 sits one step short
     * of it. The ease steps once per retrace and the game draws every other
     * retrace, so the two share one drawn frame: measured on the port, the
     * fix shortens the intro by exactly one drawn frame (150 to 149 between
     * PlayerModel_SetAnimation and the hand-off). With the fix on, the loop
     * stops at i == 8 and the eye pose is landed below, in the hand-off's
     * own tick. */
    for (i = 0; i <= lastPose; i++) {
        if (gContInputPressedButtons & (A_BUTTON | START_BUTTON)) {
            func_802E2194_6C9F74();
        }
        cam->viewMtx.lookAt.eye.x = (((f32) i * (startEyeX - baseEyeX)) / 10.0f) + baseEyeX;
        cam->viewMtx.lookAt.eye.y = (((f32) i * (startEyeY - baseEyeY)) / 10.0f) + baseEyeY;
        cam->viewMtx.lookAt.eye.z = (((f32) i * (startEyeZ - baseEyeZ)) / 10.0f) + baseEyeZ;
        cam->viewMtx.lookAt.at.x = (((f32) i * (startAtX - baseAtX)) / 10.0f) + baseAtX;
        cam->viewMtx.lookAt.at.y = (((f32) i * (startAtY - baseAtY)) / 10.0f) + baseAtY;
        cam->viewMtx.lookAt.at.z = (((f32) i * (startAtZ - baseAtZ)) / 10.0f) + baseAtZ;
        ohWait(1);
    }

    if (lastPose != 10) {
        /* The loop stopped short of the eye: land it now, so the pose the
         * hand-off resumes gameplay from is the one the ROM's loop reached,
         * with no frame drawn in between. */
        cam->viewMtx.lookAt.eye.x = startEyeX;
        cam->viewMtx.lookAt.eye.y = startEyeY;
        cam->viewMtx.lookAt.eye.z = startEyeZ;
        cam->viewMtx.lookAt.at.x = startAtX;
        cam->viewMtx.lookAt.at.y = startAtY;
        cam->viewMtx.lookAt.at.z = startAtZ;
    }
    func_802E2194_6C9F74();
    ohWait(1);
}
