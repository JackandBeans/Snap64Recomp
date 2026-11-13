/**
 * @file steam_art.h
 * @brief A Steam shortcut's library art, put in place by the port.
 *
 * Adding a game to Steam as a shortcut gives it an icon and nothing else: the
 * library shows a blank poster and a blank page until the player finds art by
 * hand. Steam reads a shortcut's art from files named after its app id in
 * userdata/<account>/config/grid, so the port can fill them itself. Steam shows the
 * art after its next restart.
 *
 * The art goes only to a shortcut in an account's shortcuts.vdf whose own entry names
 * this game: by its name in Steam, or in its target or launch options by the game's
 * name, the Flatpak's id or this executable's path. Steam's own word is not enough: a
 * game started through another shortcut, a launcher, reports that shortcut's id. So
 * SteamGameId and SteamAppId decide nothing. They are said in the log, and they say
 * why when nothing is written.
 *
 * The port never replaces a file already there and writes nowhere else in Steam's
 * folder. A picture it wrote and the player deleted stays deleted
 * (steam_art_done.txt, written only after a picture is in place). A slot that holds
 * art of the player's own is looked at again at each start, so the port's picture
 * goes in if they remove theirs.
 *
 * Every start logs one line saying what Steam passed, which shortcut names the game,
 * and what became of the pictures. A picture written or failed has a line of its
 * own too.
 *
 * Which pictures exist is the package's business: a slot with no picture in the
 * steam_art folder beside the executable is simply left alone.
 *
 * The icon is not here. Steam takes a shortcut's icon from the executable or its
 * .desktop file when the game is added and keeps its path in shortcuts.vdf, a file
 * Steam rewrites and the port does not touch.
 *
 * SNAP_NO_STEAM_ART=1 turns this off. Windows and desktop Linux; nothing elsewhere.
 */
#pragma once

namespace snap {

// Reads what the start says, then does the work on a short thread of its own and
// returns at once. Call it on the main thread.
void steam_art_start();

// The same, all on the calling thread. No failure leaves it: each is a log line.
void steam_art_run();

} // namespace snap
