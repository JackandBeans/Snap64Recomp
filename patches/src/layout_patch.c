/**
 * Replaces UILayout_UpdateButtons from src/window/layout.c -- the process
 * that slides the side panel's buttons in and out (the lab, the course
 * list, the PKMN Report, the album, the Gallery, the photo check).
 *
 * A button slides in by its scale: each frame its phase rises and the
 * button is drawn at sin(phase) of its height, until the phase reaches a
 * quarter turn. The original then stops without setting the scale itself,
 * so every button rests at the last step's value, just under one: 97.8%
 * and 99.4% of their height for the album's two (texture step 1047 and
 * 1030 where 1024 is one to one, read from the draw calls). When a choice
 * is made, UILayout_HideButtons sets the scale to exactly one for the
 * moment before each button slides out.
 *
 * At the cartridge's 320 by 240 the two are the same picture. The port's
 * renderer draws scaled 2D smoothly at the output resolution and one-to-one
 * 2D on the native grid, so the buttons were soft at rest and turned crisp,
 * half a pixel over, the instant a choice was made (found in playtesting on
 * the album, the Report and the lab, 2026-10-01). Here a button
 * that has finished sliding in is set to exactly one, at its own resting
 * place, as UILayout_HideButtons sets it a moment later; everything else is
 * the original's.
 */

#include "common.h"
#include "sp.h"
#include "macros.h"
#include "window/window.h"

/* sinf is a weak alias of __sinf (render_patch.c says why the real symbol
 * is called). */
#define sinf __sinf

extern s32 UILayout_PanelState;
extern s32 UILayout_IsAnimationHorizontal;
extern s32 UILayout_IsInstantTransition;
extern s32 UILayout_AnimationFrames;
/* Statics of layout.c, at the addresses the original function loads. */
#define UILayout_ButtonObjects ((GObj**) 0x803A6A90)   /* GObj* [BUTTON_MAX + 1] */
#define UILayout_ButtonPhase   ((f32*) 0x803A6B18)     /* f32 [BUTTON_MAX + 1] */

void UILayout_UpdateButtons(GObj* unused) {
    s32 i;
    GObj* button;
    s32 numSlidingButtons;
    f32 scale;

    while (TRUE) {
        numSlidingButtons = 0;
        for (i = 0; i <= BUTTON_MAX; i++) {
            button = UILayout_ButtonObjects[i];
            if (UILayout_PanelState == PANEL_STATE_EXPANDING) {
                if ((button->data.sobj->sprite.attr ^ SP_HIDDEN) & (SP_HIDDEN | SP_SCALE)) {
                    UILayout_ButtonPhase[i] += (f32) ((UILayout_AnimationFrames + 1) * 90.0 / 30.0) * PI / 180.0f;
                    if (UILayout_IsInstantTransition) {
                        UILayout_ButtonPhase[i] = PI_2;
                    }
                    if (UILayout_ButtonPhase[i] >= PI_2) {
                        UILayout_ButtonPhase[i] = PI_2;
                        /* The port's one change: the slide-in ends at one. */
                        if (!(button->data.sobj->sprite.attr & SP_HIDDEN)) {
                            if (UILayout_IsAnimationHorizontal) {
                                button->data.sobj->sprite.scalex = 1.0f;
                                button->data.sobj->sprite.x = button->data.sobj->unk_54;
                            } else {
                                button->data.sobj->sprite.scaley = 1.0f;
                                button->data.sobj->sprite.y = button->data.sobj->unk_54;
                            }
                        }
                    } else {
                        scale = sinf(UILayout_ButtonPhase[i]);

                        if (scale < 0.001) {
                            scale = 0.001f;
                        }
                        if (UILayout_ButtonPhase[i] >= 0.0f) {
                            button->data.sobj->sprite.attr &= ~SP_HIDDEN;
                            if (UILayout_IsAnimationHorizontal) {
                                button->data.sobj->sprite.scalex = scale;
                                button->data.sobj->sprite.x = button->data.sobj->unk_54 + (1.0 - scale) * 86.0;
                            } else {
                                button->data.sobj->sprite.scaley = scale;
                                button->data.sobj->sprite.y = button->data.sobj->unk_54 + (1.0 - scale) * 15.0;
                            }
                        }
                        numSlidingButtons++;
                    }
                }
            } else if (UILayout_PanelState == PANEL_STATE_COLLAPSING) {
                if ((button->data.sobj->sprite.attr ^ SP_HIDDEN) & (SP_HIDDEN | SP_SCALE)) {
                    UILayout_ButtonPhase[i] -= (f32) ((UILayout_AnimationFrames + 1) * 90.0 / 30.0) * PI / 180.0f;
                    if (UILayout_IsInstantTransition) {
                        UILayout_ButtonPhase[i] = -PI / 1800.0f;
                    }
                    if (UILayout_ButtonPhase[i] <= 0.0f) {
                        button->data.sobj->sprite.attr |= SP_HIDDEN;
                        button->data.sobj->sprite.attr &= ~SP_SCALE;
                        if (UILayout_IsAnimationHorizontal) {
                            button->data.sobj->sprite.scalex = 1.0f;
                        } else {
                            button->data.sobj->sprite.scaley = 1.0f;
                        }
                        UILayout_ButtonPhase[i] = PI / 180.0f;
                    } else {
                        scale = sinf(UILayout_ButtonPhase[i]);
                        if (scale < 0.001) {
                            scale = 0.001f;
                        }
                        if (UILayout_ButtonPhase[i] < PI_2) {
                            if (UILayout_IsAnimationHorizontal) {
                                button->data.sobj->sprite.scalex = scale;
                                button->data.sobj->sprite.x = button->data.sobj->unk_54 + (1.0 - scale) * 86.0;
                            } else {
                                button->data.sobj->sprite.scaley = scale;
                                button->data.sobj->sprite.y = button->data.sobj->unk_54 + (1.0 - scale) * 15.0;
                            }
                            button->data.sobj->sprite.attr |= SP_SCALE;
                        }
                        numSlidingButtons++;
                    }
                }
            }
        }
        if (numSlidingButtons == 0) {
            UILayout_PanelState = PANEL_STATE_STABLE;
        }
        ohWait(1);
    }
}
