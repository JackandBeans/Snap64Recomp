/**
 * @file paths.cpp
 * @brief See paths.h.
 */
#include "paths.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <system_error>
#if defined(__linux__)
#include <unistd.h>
#endif

#include <SDL2/SDL_stdinc.h>
#include <SDL2/SDL_filesystem.h>

namespace snap {

const std::filesystem::path& base_dir() {
    static const std::filesystem::path dir = [] {
        std::filesystem::path result;
#if defined(__linux__)
        // SNAP_DATA_DIR names the directory outright (a test rig, a launcher
        // that keeps profiles apart). Otherwise the executable's own folder
        // when it can be written, which is the unpacked tarball; when it
        // cannot (a read-only mount, a system directory), the XDG config
        // home, as the other N64 recompilations use, and the ROM is looked
        // for there. The log's first line says which.
        if (const char* forced = std::getenv("SNAP_DATA_DIR")) {
            result = std::filesystem::path(forced);
            if (result.is_absolute()) {
                std::error_code ec;
                std::filesystem::create_directories(result, ec);
                return result / "";
            }
            fprintf(stderr, "[SNAP] SNAP_DATA_DIR is not an absolute path; ignored" "\n");
        }
        if (char* base = SDL_GetBasePath()) {
            std::filesystem::path exeDir(base);
            SDL_free(base);
            if (access(exeDir.c_str(), W_OK) == 0) {
                return exeDir;
            }
            std::filesystem::path home;
            if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
                home = xdg;
            } else if (const char* h = std::getenv("HOME"); h && *h) {
                home = std::filesystem::path(h) / ".config";
            }
            if (!home.empty()) {
                result = home / "Snap64Recomp";
                std::error_code ec;
                std::filesystem::create_directories(result, ec);
                fprintf(stderr, "[SNAP] %s is not writable; files resolve against %s" "\n",
                        exeDir.c_str(), result.c_str());
                return result / "";
            }
        }
#endif
        // UTF-8 with a trailing separator; the path constructor from a
        // u8string keeps non-ASCII install paths intact on Windows.
        if (char* base = SDL_GetBasePath()) {
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

const std::filesystem::path& exe_dir() {
    static const std::filesystem::path dir = [] {
        std::filesystem::path result;
        if (char* base = SDL_GetBasePath()) {
            result = std::filesystem::path(
                std::u8string(reinterpret_cast<const char8_t*>(base)));
            SDL_free(base);
        }
        if (result.empty()) {
            // The same fallback base_dir() takes, said there.
            std::error_code ec;
            result = std::filesystem::current_path(ec);
        }
        return result;
    }();
    return dir;
}

std::filesystem::path exe_path(std::string_view rel) {
    return exe_dir() / std::filesystem::path(rel);
}

} // namespace snap
