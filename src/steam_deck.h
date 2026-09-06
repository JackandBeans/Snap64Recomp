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

} // namespace snap
