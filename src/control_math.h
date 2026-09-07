#ifndef SNAP_CONTROL_MATH_H
#define SNAP_CONTROL_MATH_H

#include <cstdint>

namespace snap {

// Protected by input.cpp's mutex across SDL's main thread and controller reads.
// Keep a quick press until a controller read, even if SDL already saw release.
struct MouseSample {
    float x = 0.0f, y = 0.0f;
    uint16_t buttons = 0;
};

class MouseAccumulator {
    MouseSample pending;
    uint16_t held = 0;
    bool active = false;
public:
    void set_active(bool value) {
        if (value != active) {
            pending = {};
            held = 0;
            active = value;
        }
    }
    bool is_active() const { return active; }
    void motion(float x, float y) {
        if (active) {
            pending.x += x;
            pending.y += y;
        }
    }
    void button(uint16_t mask, bool down) {
        if (!active) return;
        if (down) {
            held |= mask;
            pending.buttons |= mask;
        } else {
            held &= ~mask;
        }
    }
    MouseSample take() {
        MouseSample result = pending;
        result.buttons |= held;
        pending = {};
        return result;
    }
};

inline void apply_mouse_aim(float& x, float& y, const MouseSample& mouse, float sensitivity) {
    x += mouse.x * sensitivity;
    y -= mouse.y * sensitivity;
}

} // namespace snap
#endif
