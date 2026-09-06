/**
 * @file steam_deck.cpp
 * @brief See steam_deck.h.
 */
#include "steam_deck.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

namespace snap {

static bool env_is_one(const char* name) {
    const char* v = std::getenv(name);
    return (v != nullptr) && (std::strcmp(v, "1") == 0);
}

bool is_steam_deck() {
    static const bool deck = [] {
        if (env_is_one("SteamDeck")) {
            return true;
        }
#if defined(__linux__)
        // The DMI board vendor: "Valve" on Jupiter (LCD) and Galileo (OLED).
        // Read once; a desktop launch outside Steam has no SteamDeck=1.
        std::ifstream in("/sys/devices/virtual/dmi/id/board_vendor");
        std::string vendor;
        if (in && std::getline(in, vendor)) {
            while (!vendor.empty() && (vendor.back() == ' ' || vendor.back() == '\r')) {
                vendor.pop_back();
            }
            return vendor == "Valve";
        }
#endif
        return false;
    }();
    return deck;
}

bool in_gamescope() {
#if defined(__linux__)
    static const bool inside = (std::getenv("GAMESCOPE_WAYLAND_DISPLAY") != nullptr);
    return inside;
#else
    return false;
#endif
}

} // namespace snap
