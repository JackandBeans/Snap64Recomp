/**
 * @file relaunch_window.h
 * @brief The window that stays up across a relaunch inside gamescope.
 *
 * The Mods page's Restart and the Snap Station replace this process's image with
 * execv. That closes the X connection, the window goes with it, and until the new
 * image draws its first frame the game has no window. Gamescope then has no game to
 * show and Steam sees the game gone: a Deck's Restart with a mod turned on came back
 * to a game that took no press for 16 seconds, a second start of the shortcut, and a
 * Steam stuck on "Exiting game" (Nov 7 2026).
 *
 * So inside gamescope the connection is left open across execv, and the new image
 * closes it once the game is running. The old window stays until then. Gamescope goes
 * on showing the window it has until the newest one has a finished frame
 * (steamcompmgr.cpp, pick_primary_focus_and_override), so the picture passes from one
 * to the other with the game never without a window.
 *
 * Outside gamescope nothing is noted, so nothing is kept. Linux desktop only; on other
 * systems this file compiles to nothing and nothing calls it.
 */
#pragma once

namespace snap {

// This run's X connection, once its window exists (steam_deck.cpp).
void relaunch_window_note_connection(int fd);

// Just before execv: lets the noted connection outlive it and tells the next image.
// False, and nothing changed, when no connection was noted. Any thread.
bool relaunch_window_keep();

// execv failed and this image goes on: as it was before relaunch_window_keep.
void relaunch_window_keep_undo();

// At the very start of an image: takes the connection the last one left, if any, and
// clears the environment's word of it, so no child of this process sees it.
void relaunch_window_take();

// Each frame, on the window's thread: closes the taken connection once `gameRunning`
// is true, or ten seconds after the relaunch whatever it says.
void relaunch_window_tick(bool gameRunning);

} // namespace snap
