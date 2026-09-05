/**
 * @file input.h
 * @brief Input callbacks for Snap64 Recomp.
 *
 * Provides SDL2 game controller, keyboard and mouse input callbacks that
 * match the ultramodern::input::callbacks_t interface, plus the main-thread
 * side of the mouse (events, capture) and the binding table the settings
 * file can rewrite.
 */

#ifndef SNAP_INPUT_H
#define SNAP_INPUT_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "ultramodern/input.hpp"

union SDL_Event;

namespace snap {

/**
 * Poll for new input events.
 * Called once per input poll cycle by the runtime.
 */
void input_poll();

/**
 * Get the current input state for a controller.
 *
 * @param controller_num  Zero-indexed controller number.
 * @param buttons         Output: N64 button bitfield.
 * @param x               Output: Analog stick X axis (-1.0 to 1.0).
 * @param y               Output: Analog stick Y axis (-1.0 to 1.0).
 * @return true if input was successfully read, false otherwise.
 */
bool input_get(int controller_num, uint16_t* buttons, float* x, float* y);

/**
 * Set rumble state for a controller.
 *
 * @param controller_num  Zero-indexed controller number.
 * @param rumble          true to enable rumble, false to disable.
 */
void input_set_rumble(int controller_num, bool rumble);

/**
 * Get information about the connected device at a controller port.
 *
 * @param controller_num  Zero-indexed controller number.
 * @return Device info struct with connected device type and pak type.
 */
ultramodern::input::connected_device_info_t input_get_connected_device_info(int controller_num);

// The mouse, main-thread side. update_gfx (src/main.cpp) hands every SDL
// event here before its own switch: mouse motion, buttons and wheel are
// taken, and the window's focus is tracked. Nothing else is consumed.
void input_handle_sdl_event(const SDL_Event& event);

// Called once per update_gfx iteration on the main thread. Captures the
// cursor (SDL relative mode: hidden, unbounded motion) while mouse aim is
// on, the window has focus and a course is running (.app_level resident),
// and releases it otherwise. Only the captured motion aims; the buttons
// and wheel work whenever the window has focus, except for the quarter
// second after focus arrives, so the click that focuses the window is not
// a press.
void input_update_mouse_capture();

// The binding table: an N64 input's name ("a", "b", "z", "start", "l", "r",
// "c_up", "c_down", "c_left", "c_right", "d_up", "d_down", "d_left",
// "d_right", "stick_up", "stick_down", "stick_left", "stick_right") to the
// sources that press it. A source is an SDL key name ("X", "Left Shift",
// "Return", "Up"; positional scancodes, as SDL_GetScancodeFromName reads
// them) or one of the mouse names "Mouse Left", "Mouse Right",
// "Mouse Middle", "Mouse X1", "Mouse X2", "Wheel Up", "Wheel Down".
using Bindings = std::map<std::string, std::vector<std::string>>;

// The layout the port ships with (the README's Controls table).
const Bindings& input_default_bindings();

// Replaces the table. Names the table does not know, and sources SDL cannot
// resolve, are reported on stdout and skipped; an input with no valid
// source keeps its default. Main thread (load_settings).
void input_set_bindings(const Bindings& bindings);

// The table in force, defaults merged with the file's entries, for writing
// back to the settings file so every name is there to edit.
Bindings input_bindings();

} // namespace snap

#endif // SNAP_INPUT_H
