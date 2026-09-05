/**
 * @file paths.cpp
 * @brief See paths.h.
 */
#include "paths.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <system_error>

#include <SDL2/SDL_stdinc.h>
#include <SDL2/SDL_filesystem.h>

namespace snap {

const std::filesystem::path& base_dir() {
    static const std::filesystem::path dir = [] {
        std::filesystem::path result;
#if defined(__APPLE__)
        // Isolate diagnostic replays from a player's saves and preferences.
        if (const char* overridePath = std::getenv("SNAP_DATA_DIR")) {
            result = std::filesystem::path(overridePath);
            std::error_code ec;
            if (!result.is_absolute()) {
                fprintf(stderr, "[SNAP] SNAP_DATA_DIR must be absolute\n");
                std::exit(1);
            }
            std::filesystem::create_directories(result, ec);
            if (ec) {
                fprintf(stderr, "[SNAP] Cannot create SNAP_DATA_DIR: %s\n", ec.message().c_str());
                std::exit(1);
            }
            return result;
        }
#endif
        // UTF-8 with a trailing separator; the path constructor from a
        // u8string keeps non-ASCII install paths intact on Windows.
#if defined(__APPLE__)
        if (char* base = SDL_GetPrefPath("", "Snap64 Recomp")) {
#else
        if (char* base = SDL_GetBasePath()) {
#endif
            result = std::filesystem::path(
                std::u8string(reinterpret_cast<const char8_t*>(base)));
            SDL_free(base);
        }
        if (result.empty()) {
            std::error_code ec;
            result = std::filesystem::current_path(ec);
            fprintf(stderr, "[SNAP] SDL_GetBasePath failed; files resolve against the working directory\n");
        }
        return result;
    }();
    return dir;
}

std::filesystem::path base_path(std::string_view rel) {
    return base_dir() / std::filesystem::path(rel);
}

} // namespace snap
