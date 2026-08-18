/**
 * @file settings.h
 * @brief Persistent user settings for PokemonSnapRecomp.
 *
 * Loaded from snapsettings.json next to the executable; applied through
 * ultramodern's GraphicsConfig (which reaches RT64 via update_config) and
 * through direct game-memory pokes for game-side options (HQ audio).
 */
#ifndef SNAP_SETTINGS_H
#define SNAP_SETTINGS_H

#include <cstdint>

namespace snap {

struct Settings {
    bool  fullscreen        = false;
    bool  widescreen        = false;  // RT64 Expand: true 16:9 FOV, not a stretch
    int   msaa              = 0;      // 0, 2, 4, 8
    // 0 = Original (native rate), 1 = Display refresh, 2 = Manual.
    //
    // Defaults to Original, which reproduces the N64 output exactly. RT64
    // interpolates by pairing each frame's transforms with the previous
    // frame's, and games written for it tag their matrices with object ids so
    // the pairing is exact. Snap ships no tags, so RT64 falls back to matching
    // on geometry, which cannot separate rows of identical vegetation quads,
    // tiled wall and sky segments, or same-species Pokemon. Press F8 to cycle
    // interpolation on; expect mispaired geometry to shimmer while it is.
    int   fps_mode          = 0;
    int   fps_manual_target = 120;
    bool  hq_sound          = true;   // pins the game's auSoundQuality flag to 1
    bool  three_point_filtering = true;
    int   downsample        = 1;
};

Settings& settings();

void load_settings();
void save_settings();

// Pushes the current settings into ultramodern's GraphicsConfig.
void apply_graphics_settings();

// Pokes game-side options into N64 memory. Safe to call repeatedly.
void apply_game_settings(uint8_t* rdram);

// Hotkey handler; returns true if the key was consumed.
bool handle_settings_hotkey(int scancode);

} // namespace snap

#endif

