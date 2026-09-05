#pragma once
#include "controller_mapping.h"
#include <mutex>
#include <atomic>
namespace snap {
extern SDL_GameController* game_controller;
extern std::recursive_mutex controller_mutex;
extern std::atomic<bool> controller_settings_active;
ControllerOptions controller_options();
ControllerSample controller_sample();
}
extern "C" {
const char* snap_controller_status();
int snap_controller_save(const char* json);
int snap_controller_test_rumble();
void snap_controller_settings_active(int active);
#if defined(__APPLE__)
void snap_settings_install(const char* (*)(), int (*)(const char*), int (*)(), void (*)(int));
void snap_settings_show();
#endif
}
