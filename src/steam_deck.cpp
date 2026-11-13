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
// A Linux desktop's alone: Android defines __linux__ too and has neither a
// Deck's board vendor to read nor an X server to ask.
#if defined(__linux__) && !defined(__ANDROID__)
#include <chrono>
#include <thread>
#include <dlfcn.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include "relaunch_window.h"
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
#if defined(__linux__) && !defined(__ANDROID__)
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
#if defined(__linux__) && !defined(__ANDROID__)
    static const bool inside = (std::getenv("GAMESCOPE_WAYLAND_DISPLAY") != nullptr);
    return inside;
#else
    return false;
#endif
}

bool gamescope_request_output_size(void* sdl_window, char* note, unsigned long cap) {
#if defined(__linux__) && !defined(__ANDROID__)
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

bool gamescope_ask_before_sdl(char* note, unsigned long cap) {
#if defined(__linux__) && !defined(__ANDROID__)
    if (!in_gamescope()) {
        snprintf(note, cap, "not inside gamescope; nothing to ask");
        return false;
    }
    void* x11 = dlopen("libX11.so.6", RTLD_NOW);
    if (x11 == nullptr) {
        snprintf(note, cap, "libX11.so.6 could not be loaded; nothing asked before SDL");
        return false;
    }
    const auto openDisplay = reinterpret_cast<decltype(&XOpenDisplay)>(dlsym(x11, "XOpenDisplay"));
    const auto closeDisplay = reinterpret_cast<decltype(&XCloseDisplay)>(dlsym(x11, "XCloseDisplay"));
    const auto internAtom = reinterpret_cast<decltype(&XInternAtom)>(dlsym(x11, "XInternAtom"));
    const auto getProperty = reinterpret_cast<decltype(&XGetWindowProperty)>(dlsym(x11, "XGetWindowProperty"));
    const auto changeProperty = reinterpret_cast<decltype(&XChangeProperty)>(dlsym(x11, "XChangeProperty"));
    const auto xsync = reinterpret_cast<decltype(&XSync)>(dlsym(x11, "XSync"));
    const auto freeData = reinterpret_cast<decltype(&XFree)>(dlsym(x11, "XFree"));
    const auto rootWindow = reinterpret_cast<decltype(&XDefaultRootWindow)>(dlsym(x11, "XDefaultRootWindow"));
    const auto getGeometry = reinterpret_cast<decltype(&XGetGeometry)>(dlsym(x11, "XGetGeometry"));
    if ((openDisplay == nullptr) || (closeDisplay == nullptr) || (internAtom == nullptr) || (getProperty == nullptr) ||
        (changeProperty == nullptr) || (xsync == nullptr) || (freeData == nullptr) || (rootWindow == nullptr) ||
        (getGeometry == nullptr)) {
        snprintf(note, cap, "libX11 lacks a function this needs; nothing asked before SDL");
        return false;
    }
    Display* display = openDisplay(nullptr);
    if (display == nullptr) {
        snprintf(note, cap, "the X server could not be reached; nothing asked before SDL");
        return false;
    }
    const Window root = rootWindow(display);
    // The server's id is published on its root; a screen without it is not
    // one gamescope made, whatever the environment says.
    const Atom idAtom = internAtom(display, "GAMESCOPE_XWAYLAND_SERVER_ID", True);
    Atom actualType = None;
    int actualFormat = 0;
    unsigned long count = 0;
    unsigned long left = 0;
    unsigned char* data = nullptr;
    if ((idAtom == None) ||
        (getProperty(display, root, idAtom, 0, 1, False, XA_CARDINAL, &actualType, &actualFormat, &count, &left, &data) != Success) ||
        (data == nullptr) || (count < 1) || (actualFormat != 32)) {
        if (data != nullptr) {
            freeData(data);
        }
        closeDisplay(display);
        snprintf(note, cap, "no GAMESCOPE_XWAYLAND_SERVER_ID on this screen; nothing asked before SDL");
        return false;
    }
    // A 32-bit property comes back as one long per item.
    const long serverId = *reinterpret_cast<long*>(data);
    freeData(data);
    const auto rootSize = [&](unsigned int* width, unsigned int* height) {
        Window rootReturned = 0;
        int x = 0;
        int y = 0;
        unsigned int border = 0;
        unsigned int depth = 0;
        return getGeometry(display, root, &rootReturned, &x, &y, width, height, &border, &depth) != 0;
    };
    unsigned int wasW = 0;
    unsigned int wasH = 0;
    rootSize(&wasW, &wasH);
    // Oversized on purpose: with the last value clear, gamescope clamps the
    // width and the height to the display's own.
    const Atom controlAtom = internAtom(display, "GAMESCOPE_XWAYLAND_MODE_CONTROL", False);
    long request[4] = { serverId, 16384, 16384, 0 };
    changeProperty(display, root, controlAtom, XA_CARDINAL, 32, PropModeReplace,
                   reinterpret_cast<unsigned char*>(request), 4);
    xsync(display, False);
    unsigned int nowW = wasW;
    unsigned int nowH = wasH;
    const auto start = std::chrono::steady_clock::now();
    long waitedMs = 0;
    // A screen that needs no change gives no sign of it, so the wait has an end.
    // A Deck's own panel is known: a screen already that size needs none at all.
    if (!(is_steam_deck() && (wasW == 1280) && (wasH == 800))) {
        while (waitedMs < 500) {
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
            waitedMs = long(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
            if (rootSize(&nowW, &nowH) && ((nowW != wasW) || (nowH != wasH))) {
                break;
            }
        }
    }
    closeDisplay(display);
    snprintf(note, cap, "asked for the display's own size for X server %ld before SDL starts; the screen was %ux%u and is %ux%u, %ld ms later",
             serverId, wasW, wasH, nowW, nowH, waitedMs);
    return true;
#else
    snprintf(note, cap, "not Linux; nothing to ask");
    return false;
#endif
}

void gamescope_note_window(void* sdl_window) {
#if defined(__linux__) && !defined(__ANDROID__)
    if (!in_gamescope() || (sdl_window == nullptr)) {
        return;
    }
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(static_cast<SDL_Window*>(sdl_window), &info) || (info.subsystem != SDL_SYSWM_X11)) {
        return;
    }
    void* x11 = dlopen("libX11.so.6", RTLD_NOW | RTLD_NOLOAD);
    if (x11 == nullptr) {
        return;
    }
    const auto connectionNumber = reinterpret_cast<decltype(&XConnectionNumber)>(dlsym(x11, "XConnectionNumber"));
    if (connectionNumber != nullptr) {
        relaunch_window_note_connection(connectionNumber(info.info.x11.display));
    }
#else
    (void)sdl_window;
#endif
}

bool gamescope_screen_size(void* sdl_window, int* w, int* h) {
#if defined(__linux__) && !defined(__ANDROID__)
    if (sdl_window == nullptr) {
        return false;
    }
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(static_cast<SDL_Window*>(sdl_window), &info) || (info.subsystem != SDL_SYSWM_X11)) {
        return false;
    }
    void* x11 = dlopen("libX11.so.6", RTLD_NOW | RTLD_NOLOAD);
    if (x11 == nullptr) {
        return false;
    }
    const auto getGeometry = reinterpret_cast<decltype(&XGetGeometry)>(dlsym(x11, "XGetGeometry"));
    const auto rootWindow = reinterpret_cast<decltype(&XDefaultRootWindow)>(dlsym(x11, "XDefaultRootWindow"));
    if ((getGeometry == nullptr) || (rootWindow == nullptr)) {
        return false;
    }
    Display* display = info.info.x11.display;
    Window root = 0;
    int x = 0;
    int y = 0;
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int border = 0;
    unsigned int depth = 0;
    if ((getGeometry(display, rootWindow(display), &root, &x, &y, &width, &height, &border, &depth) == 0) ||
        (width == 0) || (height == 0)) {
        return false;
    }
    *w = int(width);
    *h = int(height);
    return true;
#else
    (void)sdl_window;
    (void)w;
    (void)h;
    return false;
#endif
}

} // namespace snap
