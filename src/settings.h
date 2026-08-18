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
    // On, and it has to be: this game's camera lives in the projection stack,
    // not the modelview. renPrepareCameraMatrix loads the perspective matrix
    // with G_MTX_PROJECTION and then multiplies the lookat into the same
    // stack, and every camera the game creates is a non-MVIEW kind that takes
    // that path -- MTX_TYPE_LOOKAT_REFLECT_ROLL for the camera you look
    // through on a course. RT64 sees an affine matrix multiplied into the
    // projection and routes it to the view, which is correct, so the view
    // matrix is the camera and nothing else carries its motion. Turning this
    // off pins the camera: an object that does not move on its own has an
    // identical transform in both frames, so interpolating it is a no-op and
    // the world can only change at the native rate, while a Pokemon still
    // looks smooth because its own animation is in the modelview. That is
    // exactly the 280Hz-Pokemon-in-a-30Hz-world split, and it is why this is
    // no longer off.
    //
    // It was off because turning it off stopped an artifact. It did, but by
    // removing the motion that exposed one: a screen-space rejection gate in
    // matchScene was refusing to pair any transform that moved more than half
    // of NDC between frames, which during a pan is every untagged transform
    // there is. Those parts froze while their tagged siblings interpolated,
    // which is a model coming apart. The gate is gone and both of the game's
    // matrix emitters are tagged now, so there is nothing left for it to
    // hide. F4 toggles it for comparison.
    bool  interpolate_camera = true;
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

