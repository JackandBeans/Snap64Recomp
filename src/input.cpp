/**
 * @file input.cpp
 * @brief SDL2-based input implementation for WaveRace64-Recomp.
 *
 * Maps SDL2 game controller and keyboard input to N64 controller state.
 * Supports a single controller (port 0) with keyboard fallback.
 *
 * N64 button mapping:
 *   A       = SDL_CONTROLLER_BUTTON_A / Keyboard X
 *   B       = SDL_CONTROLLER_BUTTON_B / Keyboard Z
 *   Z       = SDL_CONTROLLER_BUTTON_LEFTSHOULDER / Keyboard L-Shift
 *   START   = SDL_CONTROLLER_BUTTON_START / Keyboard Return
 *   D-Up    = SDL_CONTROLLER_BUTTON_DPAD_UP / Keyboard Up
 *   D-Down  = SDL_CONTROLLER_BUTTON_DPAD_DOWN / Keyboard Down
 *   D-Left  = SDL_CONTROLLER_BUTTON_DPAD_LEFT / Keyboard Left
 *   D-Right = SDL_CONTROLLER_BUTTON_DPAD_RIGHT / Keyboard Right
 *   L       = SDL_CONTROLLER_AXIS_TRIGGERLEFT / Keyboard Q
 *   R       = SDL_CONTROLLER_AXIS_TRIGGERRIGHT / Keyboard E
 *   C-Up    = Right stick up / Keyboard I
 *   C-Down  = Right stick down / Keyboard K
 *   C-Left  = Right stick left / Keyboard J
 *   C-Right = Right stick right / Keyboard L
 *   Analog  = Left stick / Keyboard WASD
 */

#include "input.h"

#include <cstdio>
#include <cmath>
#include <SDL2/SDL.h>

// N64 controller button bits (matching libultra OS_CONT_* defines).
#define N64_BTN_A       0x8000
#define N64_BTN_B       0x4000
#define N64_BTN_Z       0x2000
#define N64_BTN_START   0x1000
#define N64_BTN_DU      0x0800
#define N64_BTN_DD      0x0400
#define N64_BTN_DL      0x0200
#define N64_BTN_DR      0x0100
// 0x0080 and 0x0040 are unused reset/reserved
#define N64_BTN_L       0x0020
#define N64_BTN_R       0x0010
#define N64_BTN_CU      0x0008
#define N64_BTN_CD      0x0004
#define N64_BTN_CL      0x0002
#define N64_BTN_CR      0x0001

namespace snap {

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static SDL_GameController* game_controller = nullptr;
static bool controller_initialized = false;

static void try_open_controller() {
    // Drop a handle whose device is gone, otherwise the stale pointer blocks
    // every future open and a replugged controller never comes back.
    if (game_controller != nullptr && !SDL_GameControllerGetAttached(game_controller)) {
        SDL_GameControllerClose(game_controller);
        game_controller = nullptr;
    }

    if (game_controller != nullptr) return;

    int num_joysticks = SDL_NumJoysticks();
    for (int i = 0; i < num_joysticks; i++) {
        if (SDL_IsGameController(i)) {
            game_controller = SDL_GameControllerOpen(i);
            if (game_controller) {
                printf("[SNAP-Input] Opened game controller: %s\n",
                       SDL_GameControllerName(game_controller));
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Public API (matches ultramodern::input::callbacks_t)
// ---------------------------------------------------------------------------

void input_poll() {
    if (!controller_initialized) {
        // SDL_Init should have been called by the gfx create callback.
        // Try to open a game controller if we haven't yet.
        try_open_controller();
        controller_initialized = true;
    }

    // Pick up newly connected controllers, and replace detached ones.
    try_open_controller();
}

bool input_get(int controller_num, uint16_t* buttons, float* x, float* y) {
    // Only support controller port 0.
    if (controller_num != 0) {
        return false;
    }

    uint16_t btn = 0;
    float ax = 0.0f;
    float ay = 0.0f;

    // -----------------------------------------------------------------------
    // Keyboard input
    // -----------------------------------------------------------------------
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    if (keys) {
        // Buttons
        if (keys[SDL_SCANCODE_X])       btn |= N64_BTN_A;
        if (keys[SDL_SCANCODE_Z])       btn |= N64_BTN_B;
        if (keys[SDL_SCANCODE_LSHIFT])  btn |= N64_BTN_Z;
        if (keys[SDL_SCANCODE_RETURN])  btn |= N64_BTN_START;
        if (keys[SDL_SCANCODE_UP])      btn |= N64_BTN_DU;
        if (keys[SDL_SCANCODE_DOWN])    btn |= N64_BTN_DD;
        if (keys[SDL_SCANCODE_LEFT])    btn |= N64_BTN_DL;
        if (keys[SDL_SCANCODE_RIGHT])   btn |= N64_BTN_DR;
        if (keys[SDL_SCANCODE_Q])       btn |= N64_BTN_L;
        if (keys[SDL_SCANCODE_E])       btn |= N64_BTN_R;
        if (keys[SDL_SCANCODE_I])       btn |= N64_BTN_CU;
        if (keys[SDL_SCANCODE_K])       btn |= N64_BTN_CD;
        if (keys[SDL_SCANCODE_J])       btn |= N64_BTN_CL;
        if (keys[SDL_SCANCODE_L])       btn |= N64_BTN_CR;

        // Analog stick from WASD
        if (keys[SDL_SCANCODE_W]) ay += 1.0f;
        if (keys[SDL_SCANCODE_S]) ay -= 1.0f;
        if (keys[SDL_SCANCODE_A]) ax -= 1.0f;
        if (keys[SDL_SCANCODE_D]) ax += 1.0f;
    }

    // -----------------------------------------------------------------------
    // Game controller input (overrides keyboard if connected)
    // -----------------------------------------------------------------------
    if (game_controller && SDL_GameControllerGetAttached(game_controller)) {
        // Buttons
        if (SDL_GameControllerGetButton(game_controller, SDL_CONTROLLER_BUTTON_A))
            btn |= N64_BTN_A;
        if (SDL_GameControllerGetButton(game_controller, SDL_CONTROLLER_BUTTON_B))
            btn |= N64_BTN_B;
        // X = B on N64 (alternative)
        if (SDL_GameControllerGetButton(game_controller, SDL_CONTROLLER_BUTTON_X))
            btn |= N64_BTN_B;
        if (SDL_GameControllerGetButton(game_controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))
            btn |= N64_BTN_Z;
        if (SDL_GameControllerGetButton(game_controller, SDL_CONTROLLER_BUTTON_START))
            btn |= N64_BTN_START;
        if (SDL_GameControllerGetButton(game_controller, SDL_CONTROLLER_BUTTON_DPAD_UP))
            btn |= N64_BTN_DU;
        if (SDL_GameControllerGetButton(game_controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN))
            btn |= N64_BTN_DD;
        if (SDL_GameControllerGetButton(game_controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT))
            btn |= N64_BTN_DL;
        if (SDL_GameControllerGetButton(game_controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT))
            btn |= N64_BTN_DR;

        // Triggers → L/R
        int16_t lt = SDL_GameControllerGetAxis(game_controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        int16_t rt = SDL_GameControllerGetAxis(game_controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        if (lt > 8000)  btn |= N64_BTN_L;
        if (rt > 8000)  btn |= N64_BTN_R;

        // Right stick → C buttons (threshold-based)
        int16_t rx = SDL_GameControllerGetAxis(game_controller, SDL_CONTROLLER_AXIS_RIGHTX);
        int16_t ry = SDL_GameControllerGetAxis(game_controller, SDL_CONTROLLER_AXIS_RIGHTY);
        constexpr int16_t C_THRESHOLD = 16000;
        if (ry < -C_THRESHOLD) btn |= N64_BTN_CU;
        if (ry >  C_THRESHOLD) btn |= N64_BTN_CD;
        if (rx < -C_THRESHOLD) btn |= N64_BTN_CL;
        if (rx >  C_THRESHOLD) btn |= N64_BTN_CR;

        // Left stick → analog
        int16_t lx = SDL_GameControllerGetAxis(game_controller, SDL_CONTROLLER_AXIS_LEFTX);
        int16_t ly = SDL_GameControllerGetAxis(game_controller, SDL_CONTROLLER_AXIS_LEFTY);

        // Normalize to -1.0..1.0 range.
        float gc_x = static_cast<float>(lx) / 32767.0f;
        float gc_y = static_cast<float>(-ly) / 32767.0f; // Invert Y (SDL Y+ is down)

        // Apply deadzone.
        constexpr float DEADZONE = 0.15f;
        if (std::fabs(gc_x) > DEADZONE) ax = gc_x;
        if (std::fabs(gc_y) > DEADZONE) ay = gc_y;
    }

    // Clamp analog values.
    ax = std::fmax(-1.0f, std::fmin(1.0f, ax));
    ay = std::fmax(-1.0f, std::fmin(1.0f, ay));

    *buttons = btn;
    *x = ax;
    *y = ay;
    return true;
}

void input_set_rumble(int controller_num, bool rumble) {
    if (controller_num != 0 || !game_controller) return;

#if SDL_VERSION_ATLEAST(2, 0, 9)
    if (rumble) {
        SDL_GameControllerRumble(game_controller, 0xFFFF, 0xFFFF, 100);
    } else {
        SDL_GameControllerRumble(game_controller, 0, 0, 0);
    }
#else
    (void)rumble;
#endif
}

ultramodern::input::connected_device_info_t input_get_connected_device_info(int controller_num) {
    if (controller_num != 0) {
        return {
            .connected_device = ultramodern::input::Device::None,
            .connected_pak    = ultramodern::input::Pak::None,
        };
    }

    // Report what is actually plugged in. Claiming a Rumble Pak with no
    // controller attached sends the game down pak init and probe paths that
    // never run on hardware without one.
    const bool attached = (game_controller != nullptr) && SDL_GameControllerGetAttached(game_controller);
    return {
        .connected_device = attached ? ultramodern::input::Device::Controller
                                     : ultramodern::input::Device::None,
        .connected_pak    = attached ? ultramodern::input::Pak::RumblePak
                                     : ultramodern::input::Pak::None,
    };
}

} // namespace snap
