#pragma once
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <array>
namespace snap {
struct ControllerOptions {
    float deadzone = 0.12f;
    float sensitivity = 1.0f;
    bool invert_y = false;
    bool dpad_navigation = true;
    bool right_stick_aim = true;
    bool rumble = true;
};
struct ControllerSample {
    std::array<bool, SDL_CONTROLLER_BUTTON_MAX> buttons{};
    std::array<float, SDL_CONTROLLER_AXIS_MAX> axes{};
};
struct MappedController { uint16_t buttons = 0; float x = 0, y = 0; };
inline std::pair<float,float> controller_stick(float x, float y, const ControllerOptions& o) {
    float m = std::hypot(x,y);
    if (m <= o.deadzone) return {0,0};
    float scale = std::min(1.0f, (m-o.deadzone)/(1-o.deadzone)*o.sensitivity)/m;
    return {x*scale, y*scale*(o.invert_y ? -1:1)};
}
inline MappedController map_controller(const ControllerSample& s, const ControllerOptions& o) {
    MappedController r;
    const auto& b=s.buttons; const auto& a=s.axes;
    if (b[SDL_CONTROLLER_BUTTON_A] || a[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] > .25f) r.buttons |= 0x8000;
    if (b[SDL_CONTROLLER_BUTTON_B] || b[SDL_CONTROLLER_BUTTON_X]) r.buttons |= 0x4000;
    if (a[SDL_CONTROLLER_AXIS_TRIGGERLEFT] > .25f) r.buttons |= 0x2000;
    if (b[SDL_CONTROLLER_BUTTON_START]) r.buttons |= 0x1000;
    if (b[SDL_CONTROLLER_BUTTON_Y]) r.buttons |= 0x0004; // C-down: flute
    if (b[SDL_CONTROLLER_BUTTON_LEFTSHOULDER]) r.buttons |= 0x0002;
    if (b[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER]) r.buttons |= 0x0001;
    if (b[SDL_CONTROLLER_BUTTON_LEFTSTICK]) r.buttons |= 0x0010; // R: dash
    if (b[SDL_CONTROLLER_BUTTON_RIGHTSTICK]) r.buttons |= 0x0008; // C-up: turn
    if (b[SDL_CONTROLLER_BUTTON_DPAD_UP]) r.buttons |= 0x0800;
    if (b[SDL_CONTROLLER_BUTTON_DPAD_DOWN]) r.buttons |= 0x0400;
    if (b[SDL_CONTROLLER_BUTTON_DPAD_LEFT]) r.buttons |= 0x0200;
    if (b[SDL_CONTROLLER_BUTTON_DPAD_RIGHT]) r.buttons |= 0x0100;
    auto left=controller_stick(a[0],-a[1],o);
    auto right=controller_stick(a[2],-a[3],o);
    r.x=left.first; r.y=left.second;
    if (o.right_stick_aim) {
        if (std::hypot(right.first,right.second)>std::hypot(r.x,r.y)) {r.x=right.first;r.y=right.second;}
    } else {
        if (a[2]<-.5f) r.buttons |= 2; if(a[2]>.5f) r.buttons |= 1;
        if (a[3]<-.5f) r.buttons |= 8; if(a[3]>.5f) r.buttons |= 4;
    }
    if(o.dpad_navigation) {
        float dx=float(b[SDL_CONTROLLER_BUTTON_DPAD_RIGHT])-float(b[SDL_CONTROLLER_BUTTON_DPAD_LEFT]);
        float dy=float(b[SDL_CONTROLLER_BUTTON_DPAD_UP])-float(b[SDL_CONTROLLER_BUTTON_DPAD_DOWN]);
        if(dx || dy) {float m=std::max(1.0f,std::hypot(dx,dy));r.x=dx/m;r.y=dy/m;}
    }
    return r;
}
}
