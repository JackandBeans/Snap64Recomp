/**
 * @file steam_deck.h
 * @brief Whether this run is on a Steam Deck, and whether it is inside
 * Steam's gaming mode compositor.
 *
 * Two signals, both cheap and both used by the other N64 recompilations:
 * Steam puts SteamDeck=1 in the environment of a game it launches on a Deck
 * (on Linux and, through Proton, for the Windows build), and the Deck's
 * firmware names Valve as the board vendor, which Linux exposes under /sys.
 * Neither is a license to hide anything: the answer only picks defaults
 * (fullscreen at boot) and the wording of a log line.
 */
#pragma once

namespace snap {

// True on a Steam Deck (LCD or OLED), decided once. False everywhere else,
// including other SteamOS handhelds, whose panels differ.
bool is_steam_deck();

// True inside gamescope, Steam's gaming-mode compositor, which sizes the
// window itself and owns the frame limiter. Linux only; false elsewhere.
bool in_gamescope();

// Inside gamescope, asks it to size this window's screen to the display's
// own size. Gamescope gives each game a nested X screen of whatever size
// Steam configured for it, and for a non-Steam shortcut that is 1280x720
// unless the player finds the Game Resolution setting; the port then draws
// 16:9 and gamescope scales it onto the Deck's 16:10 panel with bars. The
// request is the same one Steam's "Native" makes: the root window property
// GAMESCOPE_XWAYLAND_MODE_CONTROL, four cardinals -- this X server's id
// (GAMESCOPE_XWAYLAND_MODE_CONTROL is read on every server's root, the id
// is published on it as GAMESCOPE_XWAYLAND_SERVER_ID), a width, a height,
// and a flag that when clear makes gamescope clamp both to the display's
// size (steamcompmgr.cpp, handle_property_notify), so an oversized ask
// means "the display's size" without knowing it: the panel on a Deck, the
// screen it is docked to otherwise. Gamescope resizes the nested screen,
// and the borderless fullscreen the port enters a moment later takes that
// size. `note` receives one line for the log. False, with the reason in
// the note, outside gamescope or when anything on the way is missing.
// Linux only.
bool gamescope_request_output_size(void* sdl_window, char* note, unsigned long cap);

// The same request before SDL starts, on a connection of its own, and the wait
// for its answer (half a second at most; none on a Deck whose screen is already
// its panel's). SDL reads the screen's size once, when it starts, and SDL 2's X11
// backend never updates the current mode of a display it knows. Asked any later,
// the window is made for the old size, and that start in Gaming Mode is the
// choppy one with a picture that shrinks: every present takes the wait's whole
// 100 ms until fullscreen is applied again. At a Snap Station relaunch on the
// same screen, where SDL reads the panel's size from the start, neither happens
// (an OLED Deck, Nov 6 2026). `note` receives one line for the log. False, with
// the reason in the note, outside gamescope or when anything on the way is
// missing. Linux only.
bool gamescope_ask_before_sdl(char* note, unsigned long cap);

// The size of this window's X screen as the X server has it now. SDL's desktop
// mode can be stale: SDL 2.32, the Flatpak's, goes on reporting the size the
// screen had when SDL started after gamescope has resized it. False when the
// window is not an X11 window or libX11 lacks what this needs. Linux only.
bool gamescope_screen_size(void* sdl_window, int* w, int* h);

// Inside gamescope, once the window exists: tells relaunch_window.h which X
// connection it is on. Nothing outside gamescope or for a window not on X11, so
// nothing is kept across a relaunch there. Linux only.
void gamescope_note_window(void* sdl_window);

} // namespace snap
