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
    // Defaults to Original, and should stay there for normal play.
    //
    // This game reads back its own rendered framebuffer: photo scoring
    // re-renders and counts pixels, and the focus indicator copies tiles of
    // the colour buffer after each Pokemon draws to decide whether one is
    // centred. Interpolation renders and presents synthetic frames around the
    // real one, so what the game reads back is no longer what it drew, and
    // those mechanics stop working -- the red dot stops appearing. F8 enables
    // it anyway for a look at high-refresh motion, at that cost.
    int   fps_mode          = 0;
    int   fps_manual_target = 120;
    bool  hq_sound          = true;   // pins the game's auSoundQuality flag to 1
    bool  three_point_filtering = true;
    // RT64 writes each rendered frame back into RDRAM, which this game needs:
    // photo scoring reads the framebuffer it just drew. Interpolated frames
    // are synthetic, so writing those back feeds invented pixels to game
    // logic. F6 toggles it to test whether that is behind an artifact.
    bool  render_to_ram     = true;
    // Interpolate the view and projection as well as object transforms.
    //
    // Off, because this game's camera is not in the view matrix: Snap carries
    // it in the modelview matrices, so every object's transform already
    // describes the camera's motion. Interpolating the view and projection on
    // top of that blends the same motion a second time on a different
    // schedule, and geometry swims against the view rather than sitting in
    // it, with whatever is near an edge falling outside -- the lower half of
    // a Pokemon disappearing while the camera pans down. Verified by toggling
    // it mid-glitch: the artifact stops the moment this is off.
    //
    // Nothing is lost by leaving it off. The camera's motion still
    // interpolates, through the object transforms that carry it. F4 toggles.
    bool  interpolate_camera = false;
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

