/**
 * @file input.h
 * @brief Input callbacks for WaveRace64-Recomp.
 *
 * Provides SDL2 game controller / keyboard input callbacks that match the
 * ultramodern::input::callbacks_t interface.
 */

#ifndef SNAP_INPUT_H
#define SNAP_INPUT_H

#include <cstdint>
#include "ultramodern/input.hpp"

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

} // namespace snap

#endif // SNAP_INPUT_H
