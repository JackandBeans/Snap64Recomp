#include "controller_support.h"
#include "paths.h"
#include "json/json.hpp"
#include <fstream>
#include <string>
namespace snap {
std::recursive_mutex controller_mutex;
std::atomic<bool> controller_settings_active{false};
static ControllerOptions options;
static bool loaded=false;
static ControllerOptions parse(const nlohmann::json& j) {
    ControllerOptions p;
    p.deadzone=std::clamp(j.value("deadzone",.12f),.02f,.4f);
    p.sensitivity=std::clamp(j.value("sensitivity",1.0f),.5f,2.0f);
    if(!std::isfinite(p.deadzone)||!std::isfinite(p.sensitivity)) throw std::runtime_error("Invalid sensitivity");
    p.invert_y=j.value("invertY",false); p.dpad_navigation=j.value("dpadNavigation",true);
    p.right_stick_aim=j.value("rightStickAim",true); p.rumble=j.value("rumble",true);
    return p;
}
static nlohmann::json serialize(const ControllerOptions& p) {
    return {{"deadzone",p.deadzone},{"sensitivity",p.sensitivity},{"invertY",p.invert_y},
        {"dpadNavigation",p.dpad_navigation},{"rightStickAim",p.right_stick_aim},{"rumble",p.rumble}};
}
ControllerOptions controller_options() {
    std::lock_guard lock(controller_mutex);
    if(!loaded) {
        loaded=true;
        try {std::ifstream f(base_path("controller.json")); if(f) options=parse(nlohmann::json::parse(f));}
        catch(const std::exception& e) {fprintf(stderr,"[Controller] Preferences: %s\n",e.what());}
    }
    return options;
}
ControllerSample controller_sample() {
    ControllerSample s;
    if(game_controller && SDL_GameControllerGetAttached(game_controller)) {
        for(int i=0;i<SDL_CONTROLLER_BUTTON_MAX;i++) s.buttons[i]=SDL_GameControllerGetButton(game_controller,SDL_GameControllerButton(i));
        for(int i=0;i<SDL_CONTROLLER_AXIS_MAX;i++) s.axes[i]=std::clamp(SDL_GameControllerGetAxis(game_controller,SDL_GameControllerAxis(i))/32767.f,-1.f,1.f);
    }
    return s;
}
}
extern "C" const char* snap_controller_status() {
    std::lock_guard lock(snap::controller_mutex);
    static thread_local std::string result;
    auto prefs=snap::controller_options();
    bool connected=snap::game_controller && SDL_GameControllerGetAttached(snap::game_controller);
    const char* name=connected?SDL_GameControllerName(snap::game_controller):nullptr;
    auto sample=snap::controller_sample();
    auto mapped=snap::map_controller(sample,prefs);
    nlohmann::json state={{"connected",connected},{"name",name?name:"No controller connected"},
        {"dualSense",connected && SDL_GameControllerGetType(snap::game_controller)==SDL_CONTROLLER_TYPE_PS5},
        {"canRumble",connected && SDL_GameControllerHasRumble(snap::game_controller)},
        {"buttons",sample.buttons},{"axes",sample.axes},{"mappedX",mapped.x},{"mappedY",mapped.y},
        {"options",snap::serialize(prefs)}};
    result=state.dump(); return result.c_str();
}
extern "C" int snap_controller_save(const char* value) {
    std::lock_guard lock(snap::controller_mutex);
    try {
        auto next=snap::parse(nlohmann::json::parse(value));
        auto temp=snap::base_path("controller.json.tmp");
        {std::ofstream f(temp);f<<snap::serialize(next).dump(2);f.flush();if(!f) return 0;}
        std::filesystem::rename(temp,snap::base_path("controller.json"));
        snap::options=next;snap::loaded=true;return 1;
    } catch(const std::exception& e) {fprintf(stderr,"[Controller] Save failed: %s\n",e.what());return 0;}
}
extern "C" int snap_controller_test_rumble() {
    std::lock_guard lock(snap::controller_mutex);
    if(!snap::game_controller || !snap::controller_options().rumble) return 0;
    return SDL_GameControllerRumble(snap::game_controller,0x4000,0x7000,250)==0;
}
extern "C" void snap_controller_settings_active(int active) { snap::controller_settings_active.store(active!=0); }
