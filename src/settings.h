/**
 * @file settings.h
 * @brief Persistent user settings for Snap64 Recomp.
 *
 * Loaded from snapsettings.json next to the executable (paths.h; the same
 * place saves/ goes, whatever the working directory); applied through
 * ultramodern's GraphicsConfig (which reaches RT64 via update_config) and
 * through direct game-memory pokes for game-side options (HQ audio).
 */
#ifndef SNAP_SETTINGS_H
#define SNAP_SETTINGS_H

#include <chrono>
#include <cstdint>
#include <mutex>

namespace snap {

struct Settings {
    bool  fullscreen        = false;
    bool  widescreen        = false;  // RT64 Expand: true 16:9 FOV, not a stretch
    int   msaa              = 0;      // 0, 2, 4, 8
    // 0 = Original (native rate), 1 = Display refresh, 2 = Manual.
    //
    // Defaults to Original, and should stay there for normal play.
    //
    // Original is the console's rate, which is reason enough under the
    // covenant. This game also reads back its own rendered framebuffer:
    // photo scoring re-renders and counts pixels, and the focus indicator
    // copies tiles of the colour buffer after each Pokemon draws. Measured
    // Sep 2026 with Display interpolation on: five photos scored and the
    // game's own pixel counts were reproduced exactly, because the readback
    // runs on the frames the game draws, not on the synthetic presents
    // between them. The focus indicator under interpolation has not been
    // re-measured since the port's focus-dot work; treat it as unverified.
    int   fps_mode          = 0;
    int   fps_manual_target = 120;
    // The game's auSoundQuality flag IS its Stereo/Mono option: zero makes
    // the audio thread average every L/R pair of the finished mix. The
    // port's SOUND page owns it now (the old name for this was hq_sound).
    bool  stereo            = true;

    // The SOUND page. Volumes are percentages in steps of ten. Master is a
    // host-side multiply on the final stream; music, effects and shutter
    // scale inside the game through patched setters that read the sound
    // mailbox bank. Background mute silences the stream while another
    // window holds focus, by zero-filling -- never by pausing the device,
    // because the game paces itself against the queue's backlog.
    int   master_volume     = 100;
    int   music_volume      = 100;
    int   sfx_volume        = 100;
    int   shutter_volume    = 100;
    bool  mute_unfocused    = false;

    bool  three_point_filtering = true;
    // Session-only diagnostic, never a saved setting. RT64 writes each
    // rendered frame back into RDRAM, and this game needs that: photo
    // scoring reads the framebuffer it just drew, and the camera's focus
    // indicator is drawn into that same framebuffer (src/focus_dot.cpp can
    // only see the dot because the rendered frame was copied back first).
    // With this off the readback never runs, every photo scores zero and
    // the indicator vanishes, and the game says nothing about either. So:
    // it is never written to snapsettings.json and never read from it (a
    // key left in an old file is ignored, and the next write drops it);
    // every boot starts with it on; and F6, which flips it to test whether
    // the readback is behind an artifact, only works when
    // snapdiag::statsEnabled() is true (SNAP_STATS=1) -- without that it
    // prints one line saying it is a diagnostic key and does nothing. While
    // it is off the window title carries " - render-to-RAM OFF (F6)".
    bool  render_to_ram     = true;
    // Overscan crop, in framebuffer pixels per side. The game never draws its
    // full 320x240 buffer: gameplay leaves dead margins (measured left 14,
    // right 16, top 12, bottom 8) and the intro's cinematics up to 30 on the
    // left, black in scenes and stale bytes in menus. Its single VI mode
    // never compensates, because every CRT it was authored for cropped the
    // picture's edges.
    //
    // Off by default: the port shows every pixel the game draws, margins
    // included, and hiding rows is an opt-in. The GRAPHICS page's Overscan
    // Crop row (mailbox byte 0x80C00014) and F2 both flip it live;
    // rt64_render_context.cpp reads the flag on every display list. The
    // per-side values are what the crop hides when it is on: the gameplay
    // margins completely, matching the classic 288x216 safe area, while the
    // intro's cinematic frame keeps a slim authored border, as it did on
    // original hardware. They stay in the file so a custom measurement
    // survives turning the crop off and on.
    bool  crop_enabled      = false;
    int   crop_left         = 16;
    int   crop_right        = 16;
    int   crop_top          = 12;
    int   crop_bottom       = 12;
    // The Beach and River intros' hand-off to first person, as the ROM
    // scripts it, eases the camera into the eye position one retrace before
    // it deletes the player model. The ease steps every retrace and the game
    // draws every other one, so the console drew one frame from inside the
    // back of Todd's head (measured: 150 drawn frames off, 149 on). Off, the
    // port draws it too, as shipped. On, the game-side patches
    // (patches/src/beach_intro_patch.c and river_intro_patch.c) end the ease
    // two poses early and land the eye pose in the hand-off's own tick. Off
    // by default: that frame is the game's own. Each patch reads this once,
    // as its intro starts, from
    // mailbox byte 0x80C00015 -- seeded from here and edited by the GRAPHICS
    // page's Cutscene Fix row.
    bool  intro_fix         = false;
    // The game shows every photo -- the review window after a shot, Oak's
    // check, the album -- by rendering it into a 320x210 buffer in RAM at
    // twice the sprite's size, halving it on the CPU (each sprite pixel the
    // average of a 2x2 block) into the sprite's own bitmap, and drawing that
    // bitmap. Off, the port draws that bitmap as the console did: the halved
    // pixels, scaled. On, the renderer recognises the bitmap as the halving
    // of a render it made and serves the sprite from a 2x2-averaged copy of
    // its own full-resolution render instead (lib/rt64/src/hle/
    // rt64_snap_photo_detail.h), so the picture Oak holds up is as sharp as
    // the ride was. Nothing else drawn as a sprite changes. Off by default:
    // the console's look.
    // Mailbox byte 0x80C00016, seeded from here and edited by the GRAPHICS
    // page's Photo Detail row; rt64_render_context.cpp copies it into
    // RT64's userConfig (snapPhotoDetail) on every config push.
    bool  photo_detail      = false;
    // Jynx's face and hands are black on the cartridge -- not a texture but
    // the primitive colour of untextured triangles. Every release Nintendo
    // has sold since the 2007 Virtual Console shows them purple. Off, the
    // port draws the cartridge. On, the renderer swaps that primitive colour
    // for the purple of Nintendo's official artwork on each draw; the
    // shading is the game's own lighting and no pixels of anyone's are
    // shipped. Off by default: the console's look.
    // Mailbox byte 0x80C00017, seeded from here and edited by the GRAPHICS
    // page's Jynx Recolour row (the J is one of the port's own glyphs, drawn
    // in the menu face's style); rt64_render_context.cpp copies it into RT64's userConfig
    // (snapJynxVC) on every config push, so a page edit applies at the next
    // display list.
    bool  jynx_vc           = false;
    // The Pokemon Snap Station on controller port 4 (snap_station.h): the
    // Blockbuster kiosk's sticker printer, which the retail cartridge knows
    // how to drive. On, the Gallery shows its Print button, and printing
    // runs the game's own photo display after a reset (a relaunch of this
    // program) and writes the sheet as PNG files under stickers/. Off by
    // default: the console had no station. Not a mailbox field yet; edited
    // in snapsettings.json.
    bool  snap_station      = false;
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

    // Internal render precision: 0 lets RT64 pick per GPU (its Automatic),
    // 1 forces the console-accurate 8-bit path, 2 forces the high-precision
    // framebuffer that removes banding in gradients. Boot-time only: the
    // shader library and every render target are built around it, and RT64
    // has no rebuild path short of setup itself.
    int   color_depth       = 0;
    // Three swap-chain images instead of two: steadier frame delivery for a
    // little latency. Also boot-time: the swap chain is created once.
    bool  triple_buffering  = false;

    // Diagnostic, not shipped as a feature. RT64 draws every call through a
    // fallback ubershader until the call's specialised pipeline finishes
    // compiling, and Snorlax's sleep symbols are visible during exactly that
    // window and never after -- the two paths disagree about one material.
    // Forcing the fallback for every draw splits the renderer in half live:
    // if the symbols show while this is on and vanish when it is off, the
    // specialised pipeline is convicted. F3 toggles it.
    bool  ubershaders_only  = false;
};

// Threading. Three threads touch the struct. The main thread -- recomp::start's
// loop, which pumps SDL events through update_gfx in src/main.cpp -- runs
// the hotkeys, the window's maximize handler, load_settings at boot and every
// disk write. The game thread runs poll_menu_mailbox once per tick. RT64's
// graphics thread reads fields on every display list
// (src/rt64_render_context.cpp).
//
// Every mutation holds settings_mutex(): handle_settings_hotkey,
// poll_menu_mailbox, load_settings and the maximize handler take it, and
// save_settings copies the struct under it, then serialises and writes the
// copy with the lock released. Readers go through settings() without the
// lock. In the language's terms that is a data race; it is tolerated on
// purpose because every field is a bool or an int, which x86-64 and ARM64
// store whole with one instruction, so a reader sees each field either
// before or after its write and never torn. What a reader can see is a mix
// of old and new fields for one frame -- a GraphicsConfig built from this
// edit's msaa and the last edit's widescreen -- and the next read corrects
// it. A std::string, or any field wider than a machine word, would break
// that promise: such a field needs the lock on the read side too.
Settings& settings();
std::mutex& settings_mutex();

// Reads snapsettings.json, or snapsettings.json.bak when the primary cannot
// be opened or the parser rejects it. The session-only fields (fullscreen,
// render_to_ram) are forced to their boot values afterwards, whatever either
// file says.
void load_settings();

// Writes the file now, through recomp::write_file_with_backup: a temporary
// file, forced to disk, then two atomic renames, so a crash at any point
// leaves either the old file or the new one complete. The previous file
// survives as snapsettings.json.bak beside it -- a side effect of that
// mechanism, and what load_settings falls back to. Returns false after
// printing "[SNAP-CFG] failed to save ..." when any step fails, and prints
// "[SNAP-CFG] saved ..." only on success. Main thread only.
bool save_settings();

// The disk write is debounced off the game tick. A page edit on the game
// thread (poll_menu_mailbox) or a hotkey marks the settings dirty; the main
// loop calls settings_flush_if_due once per iteration, and it writes when
// the settings are dirty and at least 750 ms have passed since the last
// mark, so a slider dragged through ten notches costs one write, not ten.
// F5 and the exit write without waiting (the exit only when dirty). A write
// that failed is not retried until the next mark, F5 or the exit.
void settings_mark_dirty();
bool settings_dirty();
bool settings_flush_if_due(std::chrono::steady_clock::time_point now);

// Pushes the current settings into ultramodern's GraphicsConfig.
void apply_graphics_settings();

// Pokes game-side options into N64 memory. Safe to call repeatedly.
void apply_game_settings(uint8_t* rdram);

// Hotkey handler; returns true if the key was consumed.
bool handle_settings_hotkey(int scancode);

// The in-game GRAPHICS page (patches/src/graphics_menu_patch.c). All three
// live in src/menu_assets.cpp. stage_menu_assets seeds the settings mailbox
// (every overlay load); stage_menu_strings harvests the menu font from the
// main menu's VPK0 segment and stages the pages' strips (the dmaReadVPK0
// wrapper, once that segment is resident); poll_menu_mailbox applies what
// the pages published (every game tick).
void stage_menu_assets(uint8_t* rdram);
void stage_menu_strings(uint8_t* rdram);
void poll_menu_mailbox(uint8_t* rdram);

} // namespace snap

#endif

