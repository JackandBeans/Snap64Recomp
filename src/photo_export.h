/**
 * @file photo_export.h
 * @brief Saves the photo the game is showing as a PNG.
 *
 * Every photo this game shows -- the picks after a course, Oak's check, the
 * album, the report -- is produced the same way: the window library
 * (decomp src/window/847B60.c) rebuilds the photo's saved state as objects and
 * renders them once, through renInitCameraEx, into an RGBA16 buffer of its
 * own in RDRAM, then draws that buffer as a sprite for as long as the photo
 * is on screen. With render-to-RAM on (the default; settings.h) RT64 writes
 * the rendered pixels back into that buffer, which is what lets the game
 * score photos at all. So the picture is sitting in memory at the game's own
 * resolution, and saving it is a matter of knowing where.
 *
 * Nintendo's 2007 Wii Virtual Console release added exactly this: Select in
 * the album posted the photo on screen to the Wii Message Board. Here it is
 * the P key and a controller's Back button, and the result is a PNG in
 * photos/ next to the executable.
 *
 * Nothing here writes to game memory. The observer reads the wrapped call's
 * arguments and two of the window library's own variables; the export reads
 * the buffer.
 */
#ifndef SNAP_PHOTO_EXPORT_H
#define SNAP_PHOTO_EXPORT_H

#include <cstdint>

#include "recomp.h"

// The observer. Must run BEFORE __real_renInitCameraEx, from the wrapper that
// owns the name (src/rect_census.cpp): a0..a3 and the stack arguments are
// only intact on the way in. Records the buffer, its dimensions, the region
// the camera renders into, and the photo's course when the window library's
// own variables vouch for it. Any game thread; never blocks on anything but a
// short mutex.
extern "C" void snap_photo_note_render_target(uint8_t* rdram, recomp_context* ctx);

namespace snap {

// Saves the most recently rendered photo to photos/ and prints one line:
// "[SNAP] photo saved: <path> (...)" or "[SNAP] photo not saved: <why>".
// Refuses, with the reason, when no photo has been rendered, when
// render-to-RAM is off (the buffer is then never written back), when the
// library that owns the buffer is no longer resident (its memory belongs to
// something else by then), or when the buffer was rendered so recently that
// the write-back may still be in flight. Called on the main thread by the
// P key (settings.cpp) and the controller's Back button (input.cpp); safe
// from any thread.
void export_photo(uint8_t* rdram);

// Called once per controller reading from input.cpp's tap. Advances the
// clock the freshness check reads, and under SNAP_STATS with
// SNAP_PHOTO_AUTOEXPORT=1 saves each newly rendered photo on its own, two
// readings after it was rendered, so an input replay that reaches Oak's check
// leaves PNGs behind without anyone pressing anything. Runs on the thread the
// game reads its controller on, which under the diagnostic is acceptable and
// in normal play never writes a file.
void photo_export_on_reading(uint8_t* rdram);

} // namespace snap

#endif
