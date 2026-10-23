/**
 * @file touch_controls.h
 * @brief Android's touch controls: the buttons drawn over the game
 * (Snap64TouchOverlay.java) and the drag that aims in a course.
 *
 * Elsewhere every call is a no-op and every answer is nothing.
 */

#ifndef SNAP_TOUCH_CONTROLS_H
#define SNAP_TOUCH_CONTROLS_H

#include <cstdint>

union SDL_Event;

namespace snap {

// Every event the main thread's pump hands input.cpp: a finger on the game
// (not on a button) aims in a course, and a press of a pad or a keyboard
// hides the automatic controls.
void touch_controls_event(const SDL_Event& event);

// The N64 buttons the overlay holds, for input_get.
uint16_t touch_controls_buttons();

// Whether the touch controls, or the phone's own gyroscope, may turn the
// view (input_get then applies the motion as it does the mouse's).
bool touch_controls_aiming();

#if defined(__ANDROID__)
// Whether the touch controls are on the screen now (rect_tags.cpp keeps
// the film counter out of R's corner while they are).
bool touch_controls_shown();

// Once a pass of the window's loop: a photo the game has taken since (its
// own count, read at the pad reads) is handed to the overlay, and the
// shutter is felt.
void touch_controls_tick();
#endif

// Whether a touch on the game is the pointer: on Android outside a course,
// or in one while a menu takes the mouse (the pause menu), where taps are
// clicks. A Steam Deck's screen never is.
bool touch_as_pointer(bool menuTakesMouse);

} // namespace snap

#endif
