/**
 * @file vr_openxr.h
 * @brief The headset: an OpenXR session the renderer draws for.
 *
 * Opt-in (`vr` in the settings file, or SNAP_VR=1 in the environment; SNAP_VR=0
 * forces it off). With it on, the port opens an OpenXR session on the same
 * Direct3D 12 device the renderer draws with, and the renderer replays each
 * frame once per eye into the headset's images (lib/rt64/src/hle/rt64_snap_vr.h,
 * the seam between the two). The game's own camera follows the head through
 * its own yaw and pitch (src/vr_game.cpp), so its photos, its scores and its
 * Pokemon's reactions are the cartridge's; its frame is the viewfinder while
 * zoomed in and the screen outside a course.
 *
 * Everything here fails soft: no runtime, no headset, a lost session, an
 * unsupported device -- the port logs one line and runs flat, exactly as it
 * would with the setting off.
 */
#ifndef SNAP_VR_OPENXR_H
#define SNAP_VR_OPENXR_H

#include <cstdint>

namespace plume {
    struct RenderDevice;
    struct RenderCommandQueue;
}

namespace snap {

// Whether the settings file or the environment asks for the headset.
bool vr_wanted();

// Opens the session on the renderer's device. Direct3D 12 only; on Vulkan
// (graphics_api: 1) or off Windows it logs and returns false. Called once the
// renderer is set up, on the main thread, before the game starts drawing.
bool vr_init(plume::RenderDevice* device, plume::RenderCommandQueue* queue, bool d3d12);

// Closes the session. Called after the renderer's threads have stopped.
void vr_shutdown();

// The session is open and running: the renderer draws for it.
bool vr_active();

// The controllers, as the game's buttons (the N64 bit layout input.cpp uses)
// and its stick, read by input_get beside the keyboard and the pad.
uint16_t vr_buttons();
void vr_stick(float& x, float& y);

// The head's turn from the seat's forward, in radians: yaw positive to the
// left, pitch positive upwards. False when the headset is not tracking.
bool vr_head_yaw_pitch(float& yaw, float& pitch);

// The camera the game is building its display list with, and whether it is
// the ride's rather than an intro's glide or a zoom transition, stamped with
// the game frame it belongs to (src/vr_game.cpp).
void vr_publish_camera(bool zoomed, bool rideDriving, uint32_t gameFrame,
                       const float eye[3], const float at[3], const float cartPos[3], const float cartRot[3]);

// The renderer's world mode: a course is running and the ride camera has
// run within the last ticks (or the game is paused inside one).
bool vr_world_mode();

// Once per game tick, on the game's thread: keeps the mailbox byte the cull
// patch reads in step with the world mode.
void vr_tick(uint8_t* rdram);

// The camera the display list is being built with, from the renderer's own
// camera set-up (src/vr_game.cpp, called by src/matrix_tags.cpp).
void vr_camera_from_display_list(uint8_t* rdram, uint32_t cameraAddress, uint32_t gameFrame);

} // namespace snap

#endif // SNAP_VR_OPENXR_H
