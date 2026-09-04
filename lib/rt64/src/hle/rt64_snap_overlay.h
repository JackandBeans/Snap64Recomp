//
// Pokemon Snap port: the Snap Station's printer display.
//
// The kiosk's video printer sat in the video path between the console and
// the monitor, and while it worked it showed its own picture in place of the
// game's: after each capture, the sixteen-picture grid it had collected so
// far, and after the last one, that grid under "PRINTING... PLEASE WAIT"
// with three marks that became stars as its three passes completed (footage
// of a working station, 3 September 2026). The game draws none of this --
// its display mode shows each photo and waits on the station's busy byte
// (decomp src/AA18E0.c) -- so the port's station composes those pictures
// itself (src/snap_station.cpp) and hands them here, and the presentation
// pass shows the picture instead of the game's frame for as long as one is
// set, through the same presenter the frame goes through. Nothing else in
// the renderer changes: the frame is still rendered, captured and
// interpolated; only what reaches the swap chain is swapped, the way the
// printer swapped the video.
//
#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

namespace RT64 {
    namespace SnapOverlay {
        struct State {
            std::mutex mutex;
            std::vector<uint8_t> rgba;   // width * height * 4, rows top to bottom
            uint32_t width = 0;
            uint32_t height = 0;
            bool visible = false;
            bool dirty = false;
        };

        State &state();
    }
}

// Copies the picture and shows it from the next present on; hide restores the
// game's frame. Either may be called from any thread.
extern "C" void snap_overlay_show(const uint8_t *rgba, uint32_t width, uint32_t height);
extern "C" void snap_overlay_hide();
