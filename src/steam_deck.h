/**
 * @file steam_deck.h
 * @brief Whether this run is on a Steam Deck, and whether it is inside
 * Steam's gaming mode compositor.
 *
 * Two signals, both cheap and both used by the other N64 recompilations:
 * Steam puts SteamDeck=1 in the environment of a game it launches on a Deck
 * (on Linux and, through Proton, for the Windows build), and the Deck's
 * firmware names Valve as the board vendor, which Linux exposes under /sys.
 * Neither is a licence to hide anything: the answer only picks defaults
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

} // namespace snap
