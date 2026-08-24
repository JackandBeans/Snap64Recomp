/**
 * Game-side patch: telling the renderer which poses the game meant to jump.
 *
 * The intro movie is animation data, and some of its channels are authored as
 * steps rather than curves. A step key holds one value and then changes to
 * another with nothing in between -- src/sys/anim.c installs them as
 * ANIM_TYPE_STEP and reads them back as `invDuration <= time ? target :
 * initial`. The console drew the old value on one frame and the new value on
 * the next, because that is all a step can look like at thirty frames a
 * second.
 *
 * Interpolation cannot tell that apart from motion. Handed two poses a frame
 * apart it draws the intermediate positions, and for a step those positions
 * are places the object was never in. Two of the port's oldest complaints are
 * exactly this and nothing else:
 *
 *   - the camera in Todd's hand: the leaf hanging off his left wrist has step
 *     keys of about two and a half and five and a half world units, the first
 *     landing on the same tick the camera begins the cut that ends the
 *     close-up. Blended, the prop slides out of his hand and back.
 *
 *   - the missing grass by the HAL logo: a single leaf of the movie's scenery
 *     steps just over a thousand units. Blended, it spends a frame part-way
 *     through that jump -- and the intro sets its fog wall at 989 to 1000
 *     units, so a frame of it is drawn beyond the fog and comes out solid fog
 *     colour. It does not slide; it vanishes and returns.
 *
 * The renderer had three defences against this and all three are keyed to how
 * MUCH moved: a velocity threshold, a vote across an object's matrices, and a
 * census of how many objects changed at once. A single leaf stepping is small
 * by every one of those measures, so it passed all three. Magnitude was never
 * the right question. The game knows which of its own keys are steps, so it
 * can simply say so.
 *
 * A step that changes value this tick raises a flag on its DObj, and
 * render_patch.c tags that object's matrices to be skipped rather than blended
 * for that one frame -- the same treatment a camera cut gets. The flag is
 * rewritten every tick, so it cannot persist past the frame it describes, and
 * DObj offset 0x57 holds it: a byte no code in the game reads or writes.
 */

#include "common.h"
#include "sys/om.h"
#include "sys/anim.h"

#define SQ(x) ((x) * (x))

void animUpdateModelAnimatedParams(DObj* dobj) {
    f32 value;
    AObj* aobj;

    /* Reset before anything can set it: an object whose animation has finished
       or been disabled still gets drawn, and must not carry a stale verdict. */
    dobj->unk_57 = 0;

    if (dobj->timeLeft != ANIMATION_DISABLED) {
        aobj = dobj->aobjList;

        while (aobj != NULL) {
            if (aobj->kind != ANIM_TYPE_NONE) {
                /* A step changes value on the tick its time crosses the
                   duration, and only then. Comparing across the advance below
                   catches exactly that tick, without keeping any history. */
                f32 timeBefore = aobj->time;

                if (dobj->timeLeft != ANIMATION_FINISHED) {
                    aobj->time += dobj->animSpeed;
                }

                if (aobj->kind == ANIM_TYPE_STEP && aobj->initialValue != aobj->targetValue &&
                    timeBefore < aobj->invDuration && aobj->invDuration <= aobj->time) {
                    switch (aobj->paramID) {
                        case ANIM_PARAM_ROTATION_X:
                        case ANIM_PARAM_ROTATION_Y:
                        case ANIM_PARAM_ROTATION_Z:
                        case ANIM_PARAM_POSITION_X:
                        case ANIM_PARAM_POSITION_Y:
                        case ANIM_PARAM_POSITION_Z:
                        case ANIM_PARAM_SCALE_X:
                        case ANIM_PARAM_SCALE_Y:
                        case ANIM_PARAM_SCALE_Z:
                            dobj->unk_57 = 1;
                            break;
                        default:
                            break;
                    }
                }

                if (!(dobj->obj->flags & GOBJ_FLAG_2)) {
                    switch (aobj->kind) {
                        case ANIM_TYPE_LINEAR:
                            value = aobj->initialValue + aobj->time * aobj->rate;
                            break;
                        case ANIM_TYPE_CUBIC: {
                            f32 temp_f16 = SQ(aobj->invDuration);
                            f32 temp_f12 = SQ(aobj->time);
                            f32 temp_f18 = aobj->invDuration * temp_f12;
                            f32 temp_f14 = aobj->time * temp_f12 * temp_f16;
                            f32 temp_f20 = 2.0f * temp_f14 * aobj->invDuration;
                            f32 temp_f22 = 3.0f * temp_f12 * temp_f16;
                            f32 temp_f24 = temp_f14 - temp_f18;

                            value = (aobj->initialValue * ((temp_f20 - temp_f22) + 1.0f)) +
                                    (aobj->targetValue * (temp_f22 - temp_f20)) +
                                    (aobj->rate * ((temp_f24 - temp_f18) + aobj->time)) + (aobj->targetRate * temp_f24);
                            break;
                        }
                        case ANIM_TYPE_STEP:
                            value = aobj->invDuration <= aobj->time ? aobj->targetValue : aobj->initialValue;
                            break;
                        default:
                            break;
                    }

                    switch (aobj->paramID) {
                        case ANIM_PARAM_ROTATION_X:
                            dobj->rotation.v.x = value;
                            break;
                        case ANIM_PARAM_ROTATION_Y:
                            dobj->rotation.v.y = value;
                            break;
                        case ANIM_PARAM_ROTATION_Z:
                            dobj->rotation.v.z = value;
                            break;
                        case ANIM_PARAM_4:
                            if (value < 0.0f) {
                                value = 0.0f;
                            } else if (value > 1.0f) {
                                value = 1.0f;
                            }
                            GetInterpolatedPosition(&dobj->position.v, aobj->unk_20, value);
                            break;
                        case ANIM_PARAM_POSITION_X:
                            dobj->position.v.x = value;
                            break;
                        case ANIM_PARAM_POSITION_Y:
                            dobj->position.v.y = value;
                            break;
                        case ANIM_PARAM_POSITION_Z:
                            dobj->position.v.z = value;
                            break;
                        case ANIM_PARAM_SCALE_X:
                            dobj->scale.v.x = value;
                            break;
                        case ANIM_PARAM_SCALE_Y:
                            dobj->scale.v.y = value;
                            break;
                        case ANIM_PARAM_SCALE_Z:
                            dobj->scale.v.z = value;
                            break;
                    }
                }
            }
            aobj = aobj->next;
        }

        if (dobj->timeLeft == ANIMATION_FINISHED) {
            dobj->timeLeft = ANIMATION_DISABLED;
        }
    }
}
