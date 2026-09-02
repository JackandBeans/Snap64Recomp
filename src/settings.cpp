#include "settings.h"
#include "hle/rt64_snap_diag.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <span>
#include <string>
#include <system_error>

#include <SDL2/SDL_scancode.h>

#include "json/json.hpp"
#include "librecomp/files.hpp"
#include "ultramodern/config.hpp"
#include "recomp.h"

namespace snap {
// overlay_hook.cpp
extern std::atomic<bool> g_hold_in_course;


static Settings s_settings;
static std::mutex s_settings_mutex;
static const char* SETTINGS_FILE = "snapsettings.json";
// The names recomp::write_file_with_backup derives from SETTINGS_FILE.
static const char* SETTINGS_BACKUP_SUFFIX = ".bak";
static const char* SETTINGS_TEMP_SUFFIX = ".temp";

// Dirtiness is a pair of generation counters rather than a flag, so an edit
// that lands while a write is in flight is never cleared by that write:
// save_settings reads the edit generation before it copies the struct, and a
// mark that arrives later leaves the two unequal, which is what dirty means.
// The failed generation keeps a write that cannot succeed from being retried
// on every main-loop iteration; the next mark, F5 or the exit tries again.
// save_settings itself runs on the main thread only (F5, the debounced flush
// and the exit), so the saved and failed generations have one writer.
static std::atomic<uint64_t> s_edit_gen{0};
static std::atomic<uint64_t> s_saved_gen{0};
static std::atomic<uint64_t> s_failed_gen{0};
static std::atomic<std::chrono::steady_clock::rep> s_last_mark{0};

// A slider notch on the GRAPHICS or SOUND page marks the settings once per
// accepted step; the write waits this long after the last of them.
static constexpr std::chrono::milliseconds SETTINGS_DEBOUNCE{750};

// Set by the overlay hook on first call; used for game-memory pokes.
uint8_t* g_rdram = nullptr;

// Snap's own audio-quality flag (src/sys/audio.c). 0 halves the sample rate.
static constexpr uint32_t AU_SOUND_QUALITY = 0x800423C0;

Settings& settings() { return s_settings; }

std::mutex& settings_mutex() { return s_settings_mutex; }

void settings_mark_dirty() {
    // The timestamp goes first and the generation is released after it, so a
    // flush that sees the new generation also sees when it was marked.
    s_last_mark.store(std::chrono::steady_clock::now().time_since_epoch().count(), std::memory_order_relaxed);
    s_edit_gen.fetch_add(1, std::memory_order_release);
}

bool settings_dirty() {
    return s_edit_gen.load(std::memory_order_acquire) != s_saved_gen.load(std::memory_order_relaxed);
}

enum class SettingsRead { Ok, Unopenable, Rejected };

// Reads path into out, starting from the fields out already holds so a key
// the file lacks keeps its value. Unopenable covers a missing file as well as
// one that cannot be read; Rejected is nlohmann refusing it -- a syntax
// error, an empty file, or a key holding the wrong type -- with why set to
// its message. out is untouched unless the whole file was accepted.
static SettingsRead read_settings_file(const std::filesystem::path& path, Settings& out, std::string& why,
                                       bool& carried_render_to_ram) {
    std::ifstream f(path);
    if (!f.good()) {
        return SettingsRead::Unopenable;
    }
    try {
        nlohmann::json j;
        f >> j;
        Settings s = out;
        s.widescreen         = j.value("widescreen", s.widescreen);
        s.msaa               = j.value("msaa", s.msaa);
        s.fps_mode           = j.value("fps_mode", s.fps_mode);
        s.fps_manual_target  = j.value("fps_manual_target", s.fps_manual_target);
        // "stereo" is the honest name; "hq_sound" was the same flag before
        // the SOUND page existed, so an old file still reads correctly.
        s.stereo             = j.value("stereo", j.value("hq_sound", s.stereo));
        s.master_volume      = std::clamp(j.value("master_volume", s.master_volume), 0, 100);
        s.music_volume       = std::clamp(j.value("music_volume", s.music_volume), 0, 100);
        s.sfx_volume         = std::clamp(j.value("sfx_volume", s.sfx_volume), 0, 100);
        s.shutter_volume     = std::clamp(j.value("shutter_volume", s.shutter_volume), 0, 100);
        s.mute_unfocused     = j.value("mute_unfocused", s.mute_unfocused);
        s.three_point_filtering = j.value("three_point_filtering", s.three_point_filtering);
        s.downsample         = j.value("downsample", s.downsample);
        s.color_depth        = j.value("color_depth", s.color_depth);
        s.triple_buffering   = j.value("triple_buffering", s.triple_buffering);
        s.resolution_scale   = std::clamp(j.value("resolution_scale", s.resolution_scale), 0, 8);
        s.present_filter     = std::clamp(j.value("present_filter", s.present_filter), 0, 2);
        s.upscale_2d         = std::clamp(j.value("upscale_2d", s.upscale_2d), 0, 2);
        s.dither_noise       = j.value("dither_noise", s.dither_noise);
        s.interpolate_camera = j.value("interpolate_camera", s.interpolate_camera);
        // render_to_ram is not read: it is a session-only diagnostic (see
        // settings.h), and a file written before it became one may still
        // carry the key. load_settings drops it by rewriting the file.
        carried_render_to_ram = j.contains("render_to_ram");
        s.ubershaders_only   = j.value("ubershaders_only", s.ubershaders_only);
        s.crop_enabled       = j.value("crop_enabled", s.crop_enabled);
        // Bounded here as well as at the consumer: half of each axis always
        // survives, whatever the file says.
        s.crop_left          = std::clamp(j.value("crop_left", s.crop_left), 0, 80);
        s.crop_right         = std::clamp(j.value("crop_right", s.crop_right), 0, 80);
        s.crop_top           = std::clamp(j.value("crop_top", s.crop_top), 0, 60);
        s.crop_bottom        = std::clamp(j.value("crop_bottom", s.crop_bottom), 0, 60);
        s.intro_fix          = j.value("intro_fix", s.intro_fix);
        out = s;
        return SettingsRead::Ok;
    } catch (const std::exception& e) {
        why = e.what();
        return SettingsRead::Rejected;
    }
}

void load_settings() {
    const std::filesystem::path primary{SETTINGS_FILE};
    const std::string backup_name = std::string(SETTINGS_FILE) + SETTINGS_BACKUP_SUFFIX;
    const std::filesystem::path backup{backup_name};

    Settings loaded;
    {
        std::lock_guard<std::mutex> lock(s_settings_mutex);
        loaded = s_settings;
    }
    bool carried_render_to_ram = false;
    std::string why;
    const SettingsRead primary_read = read_settings_file(primary, loaded, why, carried_render_to_ram);
    if (primary_read != SettingsRead::Ok) {
        // The backup is what recomp::write_file_with_backup left of the
        // previous file. It is consulted only when the primary is unusable:
        // absent (a crash between the write's two renames) or rejected by
        // the parser (a truncated or hand-broken file). A primary that
        // parses is taken as it is, however old the backup.
        const char* primary_why = (primary_read == SettingsRead::Rejected) ? why.c_str() : "cannot be opened";
        std::string backup_why_text;
        const SettingsRead backup_read = read_settings_file(backup, loaded, backup_why_text, carried_render_to_ram);
        if (backup_read == SettingsRead::Ok) {
            fprintf(stderr, "[SNAP-CFG] %s unusable (%s); loaded %s instead\n",
                    SETTINGS_FILE, primary_why, backup_name.c_str());
        } else if (primary_read == SettingsRead::Rejected || backup_read == SettingsRead::Rejected) {
            const char* backup_why = (backup_read == SettingsRead::Rejected) ? backup_why_text.c_str() : "cannot be opened";
            fprintf(stderr, "[SNAP-CFG] no usable settings file (%s: %s; %s: %s); using defaults\n",
                    SETTINGS_FILE, primary_why, backup_name.c_str(), backup_why);
        }
        // Neither file can be opened: the first boot. Nothing to say.
    }

    {
        std::lock_guard<std::mutex> lock(s_settings_mutex);
        s_settings = loaded;
        // Fullscreen is a session choice, never a boot state: a window created
        // fullscreen comes up with broken chrome -- no close, no minimize, no
        // resize -- so every launch starts windowed and fullscreen is entered
        // through the live path only (the GRAPHICS page, or the window's own
        // maximize button).
        s_settings.fullscreen = false;
        // Render-to-RAM is a session-only diagnostic that the file never
        // decides: every boot starts with it on, because photo scoring reads
        // the framebuffer it copies back (see settings.h).
        s_settings.render_to_ram = true;
    }
    if (carried_render_to_ram) {
        printf("[SNAP-CFG] the settings file carries a render_to_ram key; it is a session-only diagnostic now, so the key was ignored and the next write drops it\n");
        fflush(stdout);
        settings_mark_dirty();
    }
}

// What the filesystem shows after recomp::write_file_with_backup has failed.
// librecomp prints the OS error on its own [files] line; this names the
// stage, which that line does not. The noexcept status overloads answer
// "not a directory" / "not a file" for a path they cannot examine, which
// lands on the generic wording rather than a wrong specific one.
static std::string describe_write_failure() {
    const std::string temp_name = std::string(SETTINGS_FILE) + SETTINGS_TEMP_SUFFIX;
    const std::string backup_name = std::string(SETTINGS_FILE) + SETTINGS_BACKUP_SUFFIX;
    std::error_code ec;
    if (std::filesystem::is_directory(std::filesystem::path(temp_name), ec)) {
        return temp_name + " is a directory, so the temporary file could not be created";
    }
    if (!std::filesystem::is_regular_file(std::filesystem::path(temp_name), ec)) {
        return "the temporary file " + temp_name + " could not be written";
    }
    if (std::filesystem::is_directory(std::filesystem::path(SETTINGS_FILE), ec)) {
        return std::string(SETTINGS_FILE) + " is a directory, so it could not be moved aside";
    }
    if (std::filesystem::is_directory(std::filesystem::path(backup_name), ec)) {
        return backup_name + " is a directory, so the old file could not become the backup";
    }
    return "the temporary file was written but could not be renamed into place";
}

bool save_settings() {
    // Read before the copy: a mark that lands after this leaves the
    // generations unequal, so the edit it announces is written again later
    // even when this copy already contains it. Losing an edit is the failure
    // this ordering rules out; an extra write is the price.
    const uint64_t gen = s_edit_gen.load(std::memory_order_acquire);
    Settings copy;
    {
        std::lock_guard<std::mutex> lock(s_settings_mutex);
        copy = s_settings;
    }
    // render_to_ram is deliberately absent: session-only, see settings.h.
    const nlohmann::json j{
        {"fullscreen",            copy.fullscreen},
        {"widescreen",            copy.widescreen},
        {"msaa",                  copy.msaa},
        {"fps_mode",              copy.fps_mode},
        {"fps_manual_target",     copy.fps_manual_target},
        {"stereo",                copy.stereo},
        {"master_volume",         copy.master_volume},
        {"music_volume",          copy.music_volume},
        {"sfx_volume",            copy.sfx_volume},
        {"shutter_volume",        copy.shutter_volume},
        {"mute_unfocused",        copy.mute_unfocused},
        {"three_point_filtering", copy.three_point_filtering},
        {"downsample",            copy.downsample},
        {"color_depth",           copy.color_depth},
        {"triple_buffering",      copy.triple_buffering},
        {"resolution_scale",      copy.resolution_scale},
        {"present_filter",        copy.present_filter},
        {"upscale_2d",            copy.upscale_2d},
        {"dither_noise",          copy.dither_noise},
        {"interpolate_camera",    copy.interpolate_camera},
        {"ubershaders_only",      copy.ubershaders_only},
        {"crop_enabled",          copy.crop_enabled},
        {"crop_left",             copy.crop_left},
        {"crop_right",            copy.crop_right},
        {"crop_top",              copy.crop_top},
        {"crop_bottom",           copy.crop_bottom},
        {"intro_fix",             copy.intro_fix},
    };
    // dump() throws only for a string that is not valid UTF-8, and every
    // value above is a bool or an int.
    std::string text = j.dump(2);
    text += '\n';
    // Temporary file, forced to disk, then two atomic renames: the previous
    // file becomes snapsettings.json.bak and the temporary file takes the
    // name, so a crash at any point leaves a complete file under one of the
    // two names. The .bak beside the file is that mechanism's side effect,
    // and what load_settings falls back to.
    if (!recomp::write_file_with_backup(std::filesystem::path(SETTINGS_FILE),
                                        std::span<const char>(text.data(), text.size()))) {
        s_failed_gen.store(gen, std::memory_order_relaxed);
        fprintf(stderr, "[SNAP-CFG] failed to save %s: %s (the [files] line above has the OS error)\n",
                SETTINGS_FILE, describe_write_failure().c_str());
        return false;
    }
    s_saved_gen.store(gen, std::memory_order_relaxed);
    printf("[SNAP-CFG] saved %s\n", SETTINGS_FILE);
    fflush(stdout);
    return true;
}

bool settings_flush_if_due(std::chrono::steady_clock::time_point now) {
    const uint64_t gen = s_edit_gen.load(std::memory_order_acquire);
    if (gen == s_saved_gen.load(std::memory_order_relaxed) ||
        gen == s_failed_gen.load(std::memory_order_relaxed)) {
        return false;
    }
    const std::chrono::steady_clock::time_point last_mark{
        std::chrono::steady_clock::duration{s_last_mark.load(std::memory_order_relaxed)}};
    // A mark that landed after the caller sampled now makes this negative,
    // which reads as "not yet" and is right.
    if (now - last_mark < SETTINGS_DEBOUNCE) {
        return false;
    }
    return save_settings();
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
    // Zero makes auThreadMain average every L/R pair of the finished mix --
    // the game's own Stereo/Mono option, owned by the SOUND page now.
    MEM_W(0, (gpr)(int32_t)AU_SOUND_QUALITY) = s_settings.stereo ? 1 : 0;
}

// Flips one flag under the settings lock and returns its new value.
static bool toggle_locked(bool Settings::*flag) {
    std::lock_guard<std::mutex> lock(s_settings_mutex);
    s_settings.*flag = !(s_settings.*flag);
    return s_settings.*flag;
}

bool handle_settings_hotkey(int scancode) {
    switch (scancode) {
        case SDL_SCANCODE_F11:
            toggle_locked(&Settings::fullscreen);
            apply_graphics_settings();
            settings_mark_dirty();
            return true;
        case SDL_SCANCODE_F10:
            toggle_locked(&Settings::widescreen);
            apply_graphics_settings();
            settings_mark_dirty();
            return true;
        case SDL_SCANCODE_F9:
            {
                std::lock_guard<std::mutex> lock(s_settings_mutex);
                s_settings.msaa = (s_settings.msaa == 0) ? 2 : (s_settings.msaa >= 8 ? 0 : s_settings.msaa * 2);
            }
            apply_graphics_settings();
            settings_mark_dirty();
            return true;
        case SDL_SCANCODE_F8:
            {
                std::lock_guard<std::mutex> lock(s_settings_mutex);
                s_settings.fps_mode = (s_settings.fps_mode + 1) % 3;
            }
            apply_graphics_settings();
            settings_mark_dirty();
            return true;
        case SDL_SCANCODE_F7: {
            const bool stereo = toggle_locked(&Settings::stereo);
            apply_game_settings(g_rdram);
            settings_mark_dirty();
            printf("[SNAP-CFG] speaker output: %s\n", stereo ? "stereo" : "mono");
            return true;
        }
        case SDL_SCANCODE_F3: {
            const bool on = toggle_locked(&Settings::ubershaders_only);
            settings_mark_dirty();
            printf("[SNAP-CFG] ubershaders only: %s\n", on ? "on" : "off");
            return true;
        }
        case SDL_SCANCODE_F4: {
            const bool on = toggle_locked(&Settings::interpolate_camera);
            settings_mark_dirty();
            printf("[SNAP-CFG] camera interpolation: %s\n", on ? "on" : "off");
            return true;
        }
        case SDL_SCANCODE_F6: {
            // A diagnostic key for diagnostic runs. With render-to-RAM off the
            // game's framebuffer readback never happens and every photo
            // scores zero without a word from the game, so the key is inert
            // unless the run was started for diagnostics, and the flag is
            // never marked dirty: it does not reach the file (settings.h).
            if (!snapdiag::statsEnabled()) {
                printf("[SNAP-CFG] F6 (render to RAM) is a diagnostic key and needs SNAP_STATS=1; ignored\n");
                fflush(stdout);
                return true;
            }
            const bool on = toggle_locked(&Settings::render_to_ram);
            printf("[SNAP-CFG] render to RAM: %s (this session only; never saved)\n", on ? "on" : "off");
            fflush(stdout);
            return true;
        }
        case SDL_SCANCODE_F5:
            // Writes now, dirty or not: the key exists to force the file out.
            save_settings();
            return true;
        case SDL_SCANCODE_END: {
            // Naming of the effect system's rectangles. Off by default until
            // the name is proven to identify one particle rather than a group
            // of them; interpolating on a name that does not is what smears
            // the leaves into each other.
            const bool on = !snapdiag::fxTaggingEnabled().load(std::memory_order_relaxed);
            snapdiag::fxTaggingEnabled().store(on, std::memory_order_relaxed);
            printf("[SNAP-CFG] effect sprite naming: %s\n", on ? "ON" : "off");
            fflush(stdout);
            return true;
        }
        case SDL_SCANCODE_HOME: {
            // Screen-space rectangle interpolation. See the note in
            // rt64_snap_diag.h: this says which half of the renderer an
            // artefact lives in, which reading the code has repeatedly failed
            // to settle.
            const bool on = !snapdiag::rectInterpolationEnabled().load(std::memory_order_relaxed);
            snapdiag::rectInterpolationEnabled().store(on, std::memory_order_relaxed);
            printf("[SNAP-CFG] 2D rectangle interpolation: %s\n", on ? "ON" : "off");
            fflush(stdout);
            return true;
        }
        case SDL_SCANCODE_F1: {
            // Frame holds inside a course. See the note in overlay_hook.cpp:
            // the gate refuses every verdict raised during a course's opening
            // movie, and whether that is a fault is a question for the eye
            // rather than the log.
            const bool held = !snap::g_hold_in_course.load(std::memory_order_relaxed);
            snap::g_hold_in_course.store(held, std::memory_order_relaxed);
            printf("[SNAP-CFG] frame holds inside a course: %s\n", held ? "ON" : "off");
            fflush(stdout);
            return true;
        }
        case SDL_SCANCODE_F12:
            // Marks the moment. The statistics are averaged over about two
            // seconds, which is long enough to dilute a fault lasting a few
            // frames into nothing; this ends the interval here so the report
            // that follows describes what was on screen just now.
            snapdiag::markRequestCounter().fetch_add(1, std::memory_order_relaxed);
            snapdiag::pairDumpPending().store(2, std::memory_order_relaxed);
            printf("[SNAP-CFG] marked -- see the [SNAP-MARK] report below (needs SNAP_STATS=1)\n");
            fflush(stdout);
            return true;
        case SDL_SCANCODE_F2: {
            const bool on = toggle_locked(&Settings::crop_enabled);
            settings_mark_dirty();
            printf("[SNAP-CFG] overscan crop: %s\n", on ? "on" : "off");
            return true;
        }
        default:
            return false;
    }
}

} // namespace snap

