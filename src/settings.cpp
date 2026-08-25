#include "settings.h"

#include <algorithm>
#include <cstdio>
#include <fstream>

#include <SDL2/SDL_scancode.h>

#include "json/json.hpp"
#include "ultramodern/config.hpp"
#include "recomp.h"

namespace snap {

static Settings s_settings;
static const char* SETTINGS_FILE = "snapsettings.json";

// Set by the overlay hook on first call; used for game-memory pokes.
uint8_t* g_rdram = nullptr;

// Snap's own audio-quality flag (src/sys/audio.c). 0 halves the sample rate.
static constexpr uint32_t AU_SOUND_QUALITY = 0x800423C0;

Settings& settings() { return s_settings; }

void load_settings() {
    std::ifstream f(SETTINGS_FILE);
    if (!f.good()) {
        return;
    }
    try {
        nlohmann::json j;
        f >> j;
        s_settings.fullscreen         = j.value("fullscreen", s_settings.fullscreen);
        s_settings.widescreen         = j.value("widescreen", s_settings.widescreen);
        s_settings.msaa               = j.value("msaa", s_settings.msaa);
        s_settings.fps_mode           = j.value("fps_mode", s_settings.fps_mode);
        s_settings.fps_manual_target  = j.value("fps_manual_target", s_settings.fps_manual_target);
        s_settings.hq_sound           = j.value("hq_sound", s_settings.hq_sound);
        s_settings.three_point_filtering = j.value("three_point_filtering", s_settings.three_point_filtering);
        s_settings.downsample         = j.value("downsample", s_settings.downsample);
        s_settings.interpolate_camera = j.value("interpolate_camera", s_settings.interpolate_camera);
        s_settings.render_to_ram      = j.value("render_to_ram", s_settings.render_to_ram);
        s_settings.ubershaders_only   = j.value("ubershaders_only", s_settings.ubershaders_only);
        s_settings.crop_enabled       = j.value("crop_enabled", s_settings.crop_enabled);
        // Bounded here as well as at the consumer: half of each axis always
        // survives, whatever the file says.
        s_settings.crop_left          = std::clamp(j.value("crop_left", s_settings.crop_left), 0, 80);
        s_settings.crop_right         = std::clamp(j.value("crop_right", s_settings.crop_right), 0, 80);
        s_settings.crop_top           = std::clamp(j.value("crop_top", s_settings.crop_top), 0, 60);
        s_settings.crop_bottom        = std::clamp(j.value("crop_bottom", s_settings.crop_bottom), 0, 60);
    } catch (const std::exception& e) {
        fprintf(stderr, "[SNAP-CFG] failed to parse %s: %s (using defaults)\n", SETTINGS_FILE, e.what());
    }
}

void save_settings() {
    nlohmann::json j{
        {"fullscreen",            s_settings.fullscreen},
        {"widescreen",            s_settings.widescreen},
        {"msaa",                  s_settings.msaa},
        {"fps_mode",              s_settings.fps_mode},
        {"fps_manual_target",     s_settings.fps_manual_target},
        {"hq_sound",              s_settings.hq_sound},
        {"three_point_filtering", s_settings.three_point_filtering},
        {"downsample",            s_settings.downsample},
        {"interpolate_camera",    s_settings.interpolate_camera},
        {"render_to_ram",         s_settings.render_to_ram},
        {"ubershaders_only",      s_settings.ubershaders_only},
        {"crop_enabled",          s_settings.crop_enabled},
        {"crop_left",             s_settings.crop_left},
        {"crop_right",            s_settings.crop_right},
        {"crop_top",              s_settings.crop_top},
        {"crop_bottom",           s_settings.crop_bottom},
    };
    std::ofstream f(SETTINGS_FILE);
    f << j.dump(2) << "\n";
    printf("[SNAP-CFG] saved %s\n", SETTINGS_FILE);
}

void apply_graphics_settings() {
    namespace ren = ultramodern::renderer;
    ren::GraphicsConfig cfg = ren::get_graphics_config();

    cfg.res_option = ren::Resolution::Auto;
    cfg.wm_option  = s_settings.fullscreen ? ren::WindowMode::Fullscreen
                                           : ren::WindowMode::Windowed;
    cfg.ar_option  = s_settings.widescreen ? ren::AspectRatio::Expand
                                           : ren::AspectRatio::Original;
    switch (s_settings.msaa) {
        case 8:  cfg.msaa_option = ren::Antialiasing::MSAA8X; break;
        case 4:  cfg.msaa_option = ren::Antialiasing::MSAA4X; break;
        case 2:  cfg.msaa_option = ren::Antialiasing::MSAA2X; break;
        default: cfg.msaa_option = ren::Antialiasing::None;   break;
    }
    switch (s_settings.fps_mode) {
        case 1:  cfg.rr_option = ren::RefreshRate::Display;  break;
        case 2:  cfg.rr_option = ren::RefreshRate::Manual;   break;
        default: cfg.rr_option = ren::RefreshRate::Original; break;
    }
    cfg.rr_manual_value = s_settings.fps_manual_target;
    cfg.ds_option = s_settings.downsample;

    ren::set_graphics_config(cfg);
    printf("[SNAP-CFG] applied: %s, %s, MSAA %dx, fps mode %d\n",
           s_settings.fullscreen ? "fullscreen" : "windowed",
           s_settings.widescreen ? "widescreen" : "4:3",
           s_settings.msaa, s_settings.fps_mode);
}

void apply_game_settings(uint8_t* rdram) {
    if (rdram == nullptr) {
        return;
    }
    // MEM_W-equivalent write of the 32-bit flag (sign-extended address form).
    MEM_W(0, (gpr)(int32_t)AU_SOUND_QUALITY) = s_settings.hq_sound ? 1 : 0;
}

bool handle_settings_hotkey(int scancode) {
    switch (scancode) {
        case SDL_SCANCODE_F11:
            s_settings.fullscreen = !s_settings.fullscreen;
            apply_graphics_settings();
            return true;
        case SDL_SCANCODE_F10:
            s_settings.widescreen = !s_settings.widescreen;
            apply_graphics_settings();
            return true;
        case SDL_SCANCODE_F9:
            s_settings.msaa = (s_settings.msaa == 0) ? 2 : (s_settings.msaa >= 8 ? 0 : s_settings.msaa * 2);
            apply_graphics_settings();
            return true;
        case SDL_SCANCODE_F8:
            s_settings.fps_mode = (s_settings.fps_mode + 1) % 3;
            apply_graphics_settings();
            return true;
        case SDL_SCANCODE_F7:
            s_settings.hq_sound = !s_settings.hq_sound;
            apply_game_settings(g_rdram);
            printf("[SNAP-CFG] HQ sound: %s\n", s_settings.hq_sound ? "on" : "off");
            return true;
        case SDL_SCANCODE_F3:
            s_settings.ubershaders_only = !s_settings.ubershaders_only;
            printf("[SNAP-CFG] ubershaders only: %s\n", s_settings.ubershaders_only ? "on" : "off");
            return true;
        case SDL_SCANCODE_F4:
            s_settings.interpolate_camera = !s_settings.interpolate_camera;
            printf("[SNAP-CFG] camera interpolation: %s\n", s_settings.interpolate_camera ? "on" : "off");
            return true;
        case SDL_SCANCODE_F6:
            s_settings.render_to_ram = !s_settings.render_to_ram;
            printf("[SNAP-CFG] render to RAM: %s\n", s_settings.render_to_ram ? "on" : "off");
            return true;
        case SDL_SCANCODE_F5:
            save_settings();
            return true;
        case SDL_SCANCODE_F2:
            s_settings.crop_enabled = !s_settings.crop_enabled;
            printf("[SNAP-CFG] overscan crop: %s\n", s_settings.crop_enabled ? "on" : "off");
            return true;
        default:
            return false;
    }
}

} // namespace snap

