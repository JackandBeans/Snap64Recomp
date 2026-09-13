/**
 * @file rom_picker.h
 * @brief The first run's question: where is the ROM?
 *
 * The port reads the player's own dump from the data directory as
 * pokemonsnap.z64 (paths.h). When no such file is there, ensure_rom() says so
 * in a dialog, opens the platform's own file chooser, checks the chosen file
 * the way librecomp will (byte order from the header, XXH3-64 of the
 * big-endian image against the expected hash) and copies it into place in
 * big-endian order under that name, so the question is never asked again and
 * a read-only install or a sandbox needs nothing placed by hand.
 *
 * SNAP_ROM_PICK=<path> answers the chooser with that file and shows no dialog
 * at all; SNAP_ROM_PICK=cancel answers it with Cancel. Both are for tests.
 */
#pragma once

#include <cstdint>

namespace snap {

// True when pokemonsnap.z64 is in the data directory on return, whether it
// was there already or was just copied in. False when the player declined or
// the copy could not be made; the log says which, and the game must not start.
bool ensure_rom(uint64_t expected_hash);

} // namespace snap
