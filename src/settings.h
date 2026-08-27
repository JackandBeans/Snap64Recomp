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
    // Also what puts the camera's focus indicator on screen: the game draws
    // that dot into the framebuffer in RDRAM, and src/focus_dot.cpp can only
    // see it there because the rendered frame was copied back first. Turning
    // this off takes the indicator with it.
    bool  render_to_ram     = true;
    // Overscan crop, in framebuffer pixels per side. The game never draws its
    // full 320x240 buffer: gameplay leaves dead margins (measured left 14,
    // right 16, top 12, bottom 8) and the intro's cinematics up to 30 on the
    // left, black in scenes and stale bytes in menus. Its single VI mode
    // never compensates, because every CRT it was authored for cropped the
    // picture's edges. These defaults hide the gameplay margins completely,
    // matching the classic 288x216 safe area; the intro's cinematic frame
    // keeps a slim authored border, as it did on original hardware.
    // F2 toggles the crop live; the per-side values stay in the file so a
    // custom measurement survives turning it off and on.
    bool  crop_enabled      = true;
    int   crop_left         = 16;
    int   crop_right        = 16;
    int   crop_top          = 12;
    int   crop_bottom       = 12;
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

    // Render resolution, in multiples of the game's 320x240. Zero follows the
    // window (RT64's integer window scale, the old behaviour). A fixed value
    // decouples rendering cost from window size: measured on this machine the
    // beach replay holds 280 fps at 4x (1280x960) and collapses at the ~6x a
    // 1440p window asks for, so a large window with a capped scale is the
    // difference between smooth and unplayable. The presentation path already
    // scales any render size to the window.
    int   resolution_scale  = 0;      // 0 = follow window, 1..8 = ceiling on the window scale

    // How the finished frame is put on screen: 0 nearest (raw pixels),
    // 1 linear, 2 the anti-aliased pixel scaling RT64 defaults to.
    int   present_filter    = 2;
    // What resolution 2D rectangles render at: 0 original chunky pixels,
    // 1 only content that scales anyway, 2 everything sharp.
    int   upscale_2d        = 1;
    // The console's own post-blend dither noise. A look choice.
    bool  dither_noise      = true;

    // Diagnostic, not shipped as a feature. RT64 draws every call through a
    // fallback ubershader until the call's specialised pipeline finishes
    // compiling, and Snorlax's sleep symbols are visible during exactly that
    // window and never after -- the two paths disagree about one material.
    // Forcing the fallback for every draw splits the renderer in half live:
    // if the symbols show while this is on and vanish when it is off, the
    // specialised pipeline is convicted. F3 toggles it.
    bool  ubershaders_only  = false;
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

