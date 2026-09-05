#include <unistd.h>
#include "controller_support.h"
#include "json/json.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
namespace snap {
SDL_GameController* game_controller=nullptr;
static std::filesystem::path testRoot;
std::filesystem::path base_path(std::string_view p) {return testRoot/std::string(p);}
}
int main() {
    char dir[]="/tmp/snap-controller-tests-XXXXXX";
    snap::testRoot=mkdtemp(dir);
    assert(SDL_Init(SDL_INIT_GAMECONTROLLER)==0);
    SDL_VirtualJoystickDesc desc{};
    desc.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes=SDL_CONTROLLER_AXIS_MAX;desc.nbuttons=SDL_CONTROLLER_BUTTON_MAX;
    desc.axis_mask=(1<<SDL_CONTROLLER_AXIS_MAX)-1;
    desc.button_mask=(1<<SDL_CONTROLLER_BUTTON_MAX)-1;
    desc.name="Snap DualSense Mapping Test";
    int index=SDL_JoystickAttachVirtualEx(&desc);assert(index>=0);
    snap::game_controller=SDL_GameControllerOpen(index);assert(snap::game_controller);
    SDL_Joystick* joystick=SDL_GameControllerGetJoystick(snap::game_controller);
    auto update=[&] {SDL_GameControllerUpdate();return snap::map_controller(snap::controller_sample(),snap::controller_options());};
    // SDL virtual trigger axes use the complete signed range; rest at -32768.
    SDL_JoystickSetVirtualAxis(joystick,4,-32768);SDL_JoystickSetVirtualAxis(joystick,5,-32768);
    assert(update().buttons==0);
    assert(nlohmann::json::parse(snap_controller_status())["connected"]==true);
    SDL_JoystickSetVirtualAxis(joystick,4,32767);
    assert((update().buttons & 0x2000)!=0); // L2 holds aim
    SDL_JoystickSetVirtualAxis(joystick,5,32767);
    assert((update().buttons & 0xA000)==0xA000); // R2 shoots while L2 remains held
    SDL_JoystickSetVirtualAxis(joystick,4,-32768);SDL_JoystickSetVirtualAxis(joystick,5,-32768);
    for(auto [button,mask]: {std::pair{SDL_CONTROLLER_BUTTON_A,0x8000},
       {SDL_CONTROLLER_BUTTON_B,0x4000},{SDL_CONTROLLER_BUTTON_X,0x4000},
       {SDL_CONTROLLER_BUTTON_Y,4},{SDL_CONTROLLER_BUTTON_START,0x1000},
       {SDL_CONTROLLER_BUTTON_LEFTSHOULDER,2},{SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,1},
       {SDL_CONTROLLER_BUTTON_LEFTSTICK,0x10},{SDL_CONTROLLER_BUTTON_RIGHTSTICK,8}}) {
        SDL_JoystickSetVirtualButton(joystick,button,1);assert(update().buttons==mask);
        SDL_JoystickSetVirtualButton(joystick,button,0);assert(update().buttons==0);
    }
    SDL_JoystickSetVirtualButton(joystick,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,1);
    assert(update().x==1); // Original name picker requires analog movement
    SDL_JoystickSetVirtualButton(joystick,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,0);
    SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_RIGHTY,-32768);
    assert(update().y==1);
    assert(snap_controller_save("{\"invertY\":true,\"deadzone\":0.2,\"sensitivity\":0.8}")==1);
    assert(update().y < -.79f && update().y > -.81f);
    assert(snap_controller_save("broken json")==0);
    assert(snap::controller_options().invert_y);
    SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_RIGHTY,1000);
    assert(update().y==0);
    assert(std::filesystem::exists(snap::base_path("controller.json")));
    SDL_JoystickDetachVirtual(index);
    assert(nlohmann::json::parse(snap_controller_status())["connected"]==false);
    SDL_GameControllerClose(snap::game_controller);snap::game_controller=nullptr;
    SDL_Quit();std::filesystem::remove_all(snap::testRoot);
    std::cout<<"PASS: virtual controller, triggers, face buttons, shoulders, stick clicks, D-pad navigation, right-stick aiming, preferences, invalid data and disconnect\n";
}
