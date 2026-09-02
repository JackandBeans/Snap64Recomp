/**
 * @file paths.h
 * @brief Where the port's files are: the executable's directory, never the
 * working directory.
 *
 * The ROM, snapsettings.json, saves/, mods/, the shader caches and the
 * title-screen badge all live next to Snap64Recomp.exe. A shortcut or a
 * shell with a different working directory used to lose every one of them
 * (main.cpp anchored librecomp's config path on current_path()); this is
 * the one place the answer comes from now.
 */
#ifndef SNAP_PATHS_H
#define SNAP_PATHS_H

#include <filesystem>
#include <string_view>

namespace snap {

// The directory containing the executable (SDL_GetBasePath, which is
// GetModuleFileNameW on Windows and needs no SDL_Init). Falls back to the
// working directory, with a line on stderr, if that ever fails.
const std::filesystem::path& base_dir();

// base_dir() / rel, for the relative names the sources already use.
std::filesystem::path base_path(std::string_view rel);

} // namespace snap

#endif
