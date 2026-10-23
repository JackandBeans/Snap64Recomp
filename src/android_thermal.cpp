/**
 * @file android_thermal.cpp
 * @brief Android: how close the phone is to slowing itself down for heat, in
 * the log.
 *
 * A phone that runs hot lowers its own clocks, and a report of the game
 * slowing after some minutes reads like a port fault without this line. Every
 * minute, and at each change of Android's thermal status, a [SNAP-THERMAL]
 * line gives the status and the headroom Android forecasts ten seconds ahead
 * (1.00 is where it starts to throttle). AThermal came with Android 11 (the
 * status) and 12 (the headroom); the app runs from Android 10, so both are
 * looked up at run time.
 */

#include "android_thermal.h"

#if defined(__ANDROID__)

#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

#include <dlfcn.h>

namespace snap {

namespace {

const char* status_name(int status) {
    static const char* const kNames[] = {"none", "light", "moderate", "severe", "critical", "emergency", "shutdown"};
    return ((status >= 0) && (status < 7)) ? kNames[status] : "unknown";
}

} // namespace

void android_thermal_start() {
    std::thread([] {
        using Acquire = void* (*)();
        using Status = int (*)(void*);
        using Headroom = float (*)(void*, int);
        void* lib = dlopen("libandroid.so", RTLD_NOW);
        const auto acquire = lib ? reinterpret_cast<Acquire>(dlsym(lib, "AThermal_acquireManager")) : nullptr;
        const auto status = lib ? reinterpret_cast<Status>(dlsym(lib, "AThermal_getCurrentThermalStatus")) : nullptr;
        const auto headroom = lib ? reinterpret_cast<Headroom>(dlsym(lib, "AThermal_getThermalHeadroom")) : nullptr;
        void* manager = ((acquire != nullptr) && (status != nullptr)) ? acquire() : nullptr;
        if (manager == nullptr) {
            printf("[SNAP-THERMAL] this Android does not report its temperature (that came with Android 11)\n");
            fflush(stdout);
            return;
        }
        int last = -2;
        auto lastLine = std::chrono::steady_clock::now();
        while (true) {
            const int now = status(manager);
            const auto t = std::chrono::steady_clock::now();
            if ((now != last) || (t - lastLine >= std::chrono::minutes(1))) {
                // Android answers the headroom at most about once a second;
                // a quicker question gets no number.
                const float h = (headroom != nullptr) ? headroom(manager, 10) : NAN;
                if (std::isnan(h)) {
                    printf("[SNAP-THERMAL] status %s; headroom not known\n", status_name(now));
                } else {
                    printf("[SNAP-THERMAL] status %s; headroom %.2f in ten seconds (1.00 is where the phone slows down)\n",
                           status_name(now), double(h));
                }
                fflush(stdout);
                last = now;
                lastLine = t;
            }
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }).detach();
}

} // namespace snap

#else

namespace snap {

void android_thermal_start() {}

} // namespace snap

#endif
