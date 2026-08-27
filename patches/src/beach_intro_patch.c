/**
 * Replaces func_beach_802C52EC from src/beach/55D1C0.c -- the Beach course
 * intro's hand-off to first person.
 *
 * The original eases the camera from the cinematic pose back to the gameplay
 * pose in eleven steps, waiting a tick after each, and only THEN resets the
 * camera and deletes the ZERO-ONE model:
 *
 *     for (i = 0; i <= 10; i++) { pose(i); ohWait(1); }
 *     func_beach_802C5214();               // reset camera, delete the model
 *
 * At i == 10 the pose IS the first-person eye position -- and the wait inside
 * that last iteration draws one full frame with the camera sitting inside the
 * back of Todd's head while his model is still on screen. The console showed
 * that frame too: one 30Hz tick of clipped hair and shirt polygons, invisible
 * on a CRT in motion, unmistakable at high resolution and refresh. It is an
 * off-by-one in the game's own script -- the model needed to be gone on the
 * frame the camera reached the eyes, not the frame after.
 *
 * The fix draws the dive through i == 8, the last pose from which the model
 * still reads as a shot, then lands the final pose and the hand-off in one
 * tick, so the model is gone before the camera is anywhere it could cut into
 * it. Two authored-but-broken frames are removed; every other frame, the skip
 * path, the timeout and the sound are untouched.
 */

#include "common.h"
#include "beach/beach.h"
#include "app_level/app_level.h"

void func_beach_802C51A0(DObj* dobj, s32 arg1, f32 arg2);
void func_beach_802C5214(void);
void func_beach_802C527C(GObj* obj);

void func_beach_802C52EC(GObj* obj) {
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

    cam = getMainCamera();
    gobj = getMainCamera()->obj;

    startEyeX = cam->viewMtx.lookAt.eye.x;
    startEyeY = cam->viewMtx.lookAt.eye.y;
    startEyeZ = cam->viewMtx.lookAt.eye.z;
    startAtX = cam->viewMtx.lookAt.at.x;
    startAtY = cam->viewMtx.lookAt.at.y;
    startAtZ = cam->viewMtx.lookAt.at.z;

    cam->animSpeed = 0.5f;
    animSetCameraAnimation(cam, &D_8013DA90_C9F20, 0.0f);
    proc = omCreateProcess(gobj, animUpdateCameraAnimation, 1, 1);
    PlayerModel_SetAnimation(&D_8013C580_C8A10, &D_8013CEA0_C9330, 0.0f, 0.5f);
    D_beach_802CC0E0 = 0;
    obj->fnAnimCallback = func_beach_802C51A0;
    omCreateProcess(obj, func_beach_802C527C, 0, 1);

    i = 0;
    while (D_beach_802CC0E0 == 0 && i < 290) {
        if (gContInputPressedButtons & (A_BUTTON | START_BUTTON)) {
            omEndProcess(proc);
            func_beach_802C5214();
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

    /* The original ran this ease to i == 10 and only then deleted the model.
     * Its last two poses are degenerate: i == 10 IS the first-person eye
     * position -- a full frame drawn from inside the back of Todd's head --
     * and i == 9 sits close enough that the near plane slices his hair open.
     * The console drew both; on a CRT at speed nobody saw them. Here the
     * swoop runs through i == 8, the last pose that still reads as a shot,
     * and then the landing pose and the hand-off share one tick, so the
     * model is gone before the camera is anywhere it could cut into it. */
    for (i = 0; i <= 8; i++) {
        if (gContInputPressedButtons & (A_BUTTON | START_BUTTON)) {
            func_beach_802C5214();
        }
        cam->viewMtx.lookAt.eye.x = (((f32) i * (startEyeX - baseEyeX)) / 10.0f) + baseEyeX;
        cam->viewMtx.lookAt.eye.y = (((f32) i * (startEyeY - baseEyeY)) / 10.0f) + baseEyeY;
        cam->viewMtx.lookAt.eye.z = (((f32) i * (startEyeZ - baseEyeZ)) / 10.0f) + baseEyeZ;
        cam->viewMtx.lookAt.at.x = (((f32) i * (startAtX - baseAtX)) / 10.0f) + baseAtX;
        cam->viewMtx.lookAt.at.y = (((f32) i * (startAtY - baseAtY)) / 10.0f) + baseAtY;
        cam->viewMtx.lookAt.at.z = (((f32) i * (startAtZ - baseAtZ)) / 10.0f) + baseAtZ;
        ohWait(1);
    }

    cam->viewMtx.lookAt.eye.x = startEyeX;
    cam->viewMtx.lookAt.eye.y = startEyeY;
    cam->viewMtx.lookAt.eye.z = startEyeZ;
    cam->viewMtx.lookAt.at.x = startAtX;
    cam->viewMtx.lookAt.at.y = startAtY;
    cam->viewMtx.lookAt.at.z = startAtZ;
    func_beach_802C5214();
    ohWait(1);
}
