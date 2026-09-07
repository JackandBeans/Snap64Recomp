// Exercises the production SDL-to-N64 mapping. Only unrelated photo/station
// services and the settings store are stubbed.
#include "input.h"
#include "settings.h"
#include "control_math.h"
#include "recomp.h"
#include <SDL2/SDL.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <mutex>
#include <vector>

extern "C" { std::atomic<int32_t> snap_frame_dump_pending{0}; }
namespace snap {
uint8_t* g_rdram = nullptr;
std::atomic<bool> g_app_level_resident{false};
Settings test_settings;
std::mutex test_mutex;
Settings& settings() { return test_settings; }
std::mutex& settings_mutex() { return test_mutex; }
void export_photo(uint8_t*) {}
void photo_export_on_reading(uint8_t*) {}
bool station_port4_present() { return false; }
}

static int checks = 0;
static void check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s (%s)\n", what, SDL_GetError()); std::exit(1); }
    ++checks;
}
static bool close(float a, float b) { return std::fabs(a - b) < 0.0001f; }
static void motion(int x, int y) {
    SDL_Event e{}; e.type = SDL_MOUSEMOTION; e.motion.xrel = x; e.motion.yrel = y;
    snap::input_handle_event(e);
}
static void button(uint8_t b, bool down) {
    SDL_Event e{}; e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    e.button.button = b; snap::input_handle_event(e);
}
static snap::MouseSample read() {
    snap::MouseSample s;
    check(snap::input_get(0, &s.buttons, &s.x, &s.y), "port zero available");
    return s;
}

int main(int argc, char**) {
    const bool replay = argc > 1;
    if (replay) {
        struct Reading { uint16_t buttons; float x, y; } r{0x8000, .25f, -.125f};
        FILE* f = std::fopen("controls-test.inputs", "wb");
        check(f != nullptr, "create replay fixture");
        std::fwrite(&r, sizeof(r), 1, f); std::fclose(f);
        _putenv_s("SNAP_REPLAY", "controls-test.inputs");
    } else {
        _putenv_s("SNAP_REPLAY", "");
    }
    check(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) == 0, "SDL init");
    SDL_Window* window = SDL_CreateWindow("Snap controls verification", SDL_WINDOWPOS_CENTERED,
                                         SDL_WINDOWPOS_CENTERED, 320, 240, SDL_WINDOW_SHOWN);
    check(window != nullptr, "test window");
    SDL_RaiseWindow(window);
    for (int i = 0; i < 100 && !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS); ++i) {
        SDL_PumpEvents(); SDL_Delay(10);
    }
    std::vector<uint8_t> memory(16 * 1024 * 1024);
    uint8_t* rdram = memory.data();
    snap::g_rdram = rdram;
    snap::g_app_level_resident = true;
    // Exercise an inverted preference independently of fresh-profile defaults.
    snap::settings().invert_y = true;
    snap::input_update_game_state(rdram);
    snap::input_update_window(window);
    if (replay) {
        check(SDL_GetRelativeMouseMode() == SDL_FALSE, "replay never captures mouse");
        motion(100, 100); button(SDL_BUTTON_RIGHT, true);
        auto s = read();
        check(s.buttons == 0x8000 && close(s.x, .25f) && close(s.y, -.125f),
              "replay bypasses live controls and inversion");
        s = read();
        check(s.buttons == 0 && s.x == 0 && s.y == 0, "replay EOF neutral");
    } else {
        check(SDL_GetRelativeMouseMode() == SDL_TRUE, "playing captures mouse");
        constexpr float scale = 80.5f / 127.0f;
        motion(2, 3); motion(1, -1);
        button(SDL_BUTTON_LEFT, true); button(SDL_BUTTON_LEFT, false);
        auto s = read();
        check(s.buttons == 0x8000, "quick left click survives release before controller read");
        check(close(s.x, .18f * scale) && close(s.y, .12f * scale), "accumulated mouse and inverted Y");
        s = read();
        check(s.buttons == 0 && s.x == 0 && s.y == 0, "motion and quick press consumed once");
        button(SDL_BUTTON_RIGHT, true);
        check(read().buttons == 0x2000 && read().buttons == 0x2000, "right button holds viewfinder");
        button(SDL_BUTTON_RIGHT, false);
        check(read().buttons == 0, "viewfinder releases");
        snap::settings().invert_y = false;
        motion(0, 2);
        check(close(read().y, -.12f * scale), "live normal Y");
        motion(100000, -100000); s = read();
        check(std::hypot(s.x, s.y) <= scale + .0001f, "mouse respects N64 circular stick range");
        motion(5, 5); button(SDL_BUTTON_LEFT, true);
        SDL_Event lost{}; lost.type = SDL_WINDOWEVENT; lost.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
        snap::input_handle_event(lost);
        s = read();
        check(s.buttons == 0 && s.x == 0 && s.y == 0, "focus loss clears held clicks and motion");
        check(SDL_GetRelativeMouseMode() == SDL_FALSE, "focus loss releases pointer");
        snap::input_update_window(window);
        s = read(); check(s.buttons == 0 && s.x == 0 && s.y == 0, "recapture starts neutral");
        MEM_B(0, (gpr)(int32_t)0x80382D20) = 1;
        snap::input_update_game_state(rdram); snap::input_update_window(window);
        check(SDL_GetRelativeMouseMode() == SDL_FALSE, "pause releases capture");
        motion(5, 5); button(SDL_BUTTON_LEFT, true); s = read();
        check(s.buttons == 0 && s.x == 0 && s.y == 0, "paused mouse does not navigate menus");
        MEM_B(0, (gpr)(int32_t)0x80382D20) = 0;
        snap::g_app_level_resident = false;
        snap::input_update_game_state(rdram); snap::input_update_window(window);
        check(SDL_GetRelativeMouseMode() == SDL_FALSE, "other overlays release capture");
        snap::g_app_level_resident = true;
        snap::settings().mouse_enabled = false;
        snap::input_update_game_state(rdram); snap::input_update_window(window);
        check(SDL_GetRelativeMouseMode() == SDL_FALSE, "M disable persists in a course");
    }

    SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_DestroyWindow(window); SDL_Quit();
    std::printf("PASS: %d production input checks (%s)\n", checks, replay ? "replay" : "live");
}
