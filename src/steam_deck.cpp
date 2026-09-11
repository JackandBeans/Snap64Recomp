/**
 * @file steam_deck.cpp
 * @brief See steam_deck.h.
 */
#include "steam_deck.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#if defined(__linux__)
#include <dlfcn.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif

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

bool gamescope_request_output_size(void* sdl_window, char* note, unsigned long cap) {
#if defined(__linux__)
    if (!in_gamescope() || (sdl_window == nullptr)) {
        snprintf(note, cap, "not inside gamescope; nothing to ask");
        return false;
    }
    SDL_Window* window = static_cast<SDL_Window*>(sdl_window);
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info) || (info.subsystem != SDL_SYSWM_X11)) {
        snprintf(note, cap, "the window is not an X11 window; nothing asked");
        return false;
    }
    // The libX11 SDL made the window with, never a second copy.
    void* x11 = dlopen("libX11.so.6", RTLD_NOW | RTLD_NOLOAD);
    if (x11 == nullptr) {
        x11 = dlopen("libX11.so.6", RTLD_NOW);
    }
    if (x11 == nullptr) {
        snprintf(note, cap, "libX11.so.6 could not be loaded; nothing asked");
        return false;
    }
    const auto internAtom = reinterpret_cast<decltype(&XInternAtom)>(dlsym(x11, "XInternAtom"));
    const auto getProperty = reinterpret_cast<decltype(&XGetWindowProperty)>(dlsym(x11, "XGetWindowProperty"));
    const auto changeProperty = reinterpret_cast<decltype(&XChangeProperty)>(dlsym(x11, "XChangeProperty"));
    const auto flush = reinterpret_cast<decltype(&XFlush)>(dlsym(x11, "XFlush"));
    const auto freeData = reinterpret_cast<decltype(&XFree)>(dlsym(x11, "XFree"));
    const auto rootWindow = reinterpret_cast<decltype(&XDefaultRootWindow)>(dlsym(x11, "XDefaultRootWindow"));
    if ((internAtom == nullptr) || (getProperty == nullptr) || (changeProperty == nullptr) ||
        (flush == nullptr) || (freeData == nullptr) || (rootWindow == nullptr)) {
        snprintf(note, cap, "libX11 lacks a function this needs; nothing asked");
        return false;
    }
    Display* display = info.info.x11.display;
    const Window root = rootWindow(display);
    // The server's id is published on its root; a screen without it is not
    // one gamescope made, whatever the environment says.
    const Atom idAtom = internAtom(display, "GAMESCOPE_XWAYLAND_SERVER_ID", True);
    if (idAtom == None) {
        snprintf(note, cap, "no GAMESCOPE_XWAYLAND_SERVER_ID on this screen; nothing asked");
        return false;
    }
    Atom actualType = None;
    int actualFormat = 0;
    unsigned long count = 0;
    unsigned long left = 0;
    unsigned char* data = nullptr;
    const int got = getProperty(display, root, idAtom, 0, 1, False, XA_CARDINAL,
                                &actualType, &actualFormat, &count, &left, &data);
    if ((got != Success) || (data == nullptr) || (count < 1) || (actualFormat != 32)) {
        if (data != nullptr) {
            freeData(data);
        }
        snprintf(note, cap, "GAMESCOPE_XWAYLAND_SERVER_ID could not be read; nothing asked");
        return false;
    }
    // A 32-bit property comes back as one long per item.
    const long serverId = *reinterpret_cast<long*>(data);
    freeData(data);
    int screenW = 0;
    int screenH = 0;
    SDL_DisplayMode mode;
    if (SDL_GetDesktopDisplayMode(SDL_GetWindowDisplayIndex(window), &mode) == 0) {
        screenW = mode.w;
        screenH = mode.h;
    }
    // Oversized on purpose: with the last value clear, gamescope clamps the
    // width and the height to the display's own.
    const Atom controlAtom = internAtom(display, "GAMESCOPE_XWAYLAND_MODE_CONTROL", False);
    long request[4] = { serverId, 16384, 16384, 0 };
    changeProperty(display, root, controlAtom, XA_CARDINAL, 32, PropModeReplace,
                   reinterpret_cast<unsigned char*>(request), 4);
    flush(display);
    snprintf(note, cap, "asked for the display's own size for X server %ld; the screen was %dx%d", serverId, screenW, screenH);
    return true;
#else
    (void)sdl_window;
    snprintf(note, cap, "not Linux; nothing to ask");
    return false;
#endif
}

} // namespace snap
