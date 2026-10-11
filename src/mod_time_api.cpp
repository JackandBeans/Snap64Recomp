/**
 * @file mod_time_api.cpp
 * @brief The computer's clock and calendar, for a mod that asks (snap64.h).
 *
 * The cartridge has no clock, and a mod is N64 code with no way to one. A
 * mod that wants the island to follow the player's day (Island Skies' "Your
 * clock") imports these two functions from the port, as it imports
 * recomp_printf: snap64_local_time gives the local hour, minute and second
 * packed in one word, snap64_local_date the year, month and day. Local time
 * is the operating system's, zone and daylight saving included. Both read
 * the clock on every call; nothing is cached.
 *
 * SNAP_CLOCK=HH:MM in the environment makes every call report that time of
 * day (the seconds keep running); SNAP_CLOCK=HH:MMxN starts there and runs N
 * times as fast, so a replay of three minutes can cross an evening. A test
 * switch, documented in the manual.
 */
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include "recomp.h"
#include "librecomp/helpers.hpp"
#include "librecomp/overlays.hpp"

#include "mod_api.h"

namespace {

struct LocalClock {
    int year, month, day;
    int hour, minute, second;
};

LocalClock read_clock() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    LocalClock c{};
    c.year = local.tm_year + 1900;
    c.month = local.tm_mon + 1;
    c.day = local.tm_mday;
    c.hour = local.tm_hour;
    c.minute = local.tm_min;
    c.second = local.tm_sec;
    // The test switch: SNAP_CLOCK=HH:MM holds that time of day; HH:MMxN
    // starts there and runs N times as fast as the real clock, so a replay
    // of three minutes can cross an evening.
    static int fakeStart = -1;      // seconds of the day
    static double fakeSpeed = 0.0;
    static std::chrono::steady_clock::time_point fakeSince;
    static bool fakeRead = false;
    if (!fakeRead) {
        fakeRead = true;
        if (const char* fake = std::getenv("SNAP_CLOCK")) {
            int h = 0;
            int m = 0;
            double speed = 0.0;
            const int n = std::sscanf(fake, "%d:%dx%lf", &h, &m, &speed);
            if ((n >= 2) && (h >= 0) && (h < 24) && (m >= 0) && (m < 60)) {
                fakeStart = h * 3600 + m * 60;
                fakeSpeed = (n == 3 && speed > 0.0) ? speed : 0.0;
                fakeSince = std::chrono::steady_clock::now();
                if (fakeSpeed > 0.0) {
                    std::printf("[SNAP-CLOCK] the clock a mod reads starts at %02d:%02d and runs %g times as fast (SNAP_CLOCK)" "\n", h, m, fakeSpeed);
                } else {
                    std::printf("[SNAP-CLOCK] the clock a mod reads is held at %02d:%02d (SNAP_CLOCK)" "\n", h, m);
                }
                std::fflush(stdout);
            }
        }
    }
    if (fakeStart >= 0) {
        double seconds = fakeStart;
        if (fakeSpeed > 0.0) {
            const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - fakeSince).count();
            seconds += elapsed * fakeSpeed;
        } else {
            seconds += c.second;
        }
        const long long whole = (long long)(seconds) % 86400;
        c.hour = int(whole / 3600);
        c.minute = int((whole / 60) % 60);
        c.second = int(whole % 60);
    }
    return c;
}

// u32 snap64_local_time(void): hour << 16 | minute << 8 | second.
void snap64_local_time(uint8_t* /*rdram*/, recomp_context* ctx) {
    const LocalClock c = read_clock();
    _return<uint32_t>(ctx, (uint32_t(c.hour) << 16) | (uint32_t(c.minute) << 8) | uint32_t(c.second));
}

// s32 snap64_utc_offset(void): the local clock's minutes east of UTC, daylight
// saving included, negative to the west (New York in October: -240).
void snap64_utc_offset(uint8_t* /*rdram*/, recomp_context* ctx) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    std::tm utc{};
#if defined(_WIN32)
    localtime_s(&local, &now);
    gmtime_s(&utc, &now);
#else
    localtime_r(&now, &local);
    gmtime_r(&now, &utc);
#endif
    int minutes = (local.tm_hour * 60 + local.tm_min) - (utc.tm_hour * 60 + utc.tm_min);
    if (local.tm_yday != utc.tm_yday) {
        const bool localLater = (local.tm_year > utc.tm_year) || ((local.tm_year == utc.tm_year) && (local.tm_yday > utc.tm_yday));
        minutes += localLater ? 1440 : -1440;
    }
    _return<int32_t>(ctx, minutes);
}

// u32 snap64_local_date(void): year << 16 | month << 8 | day.
void snap64_local_date(uint8_t* /*rdram*/, recomp_context* ctx) {
    const LocalClock c = read_clock();
    _return<uint32_t>(ctx, (uint32_t(c.year) << 16) | (uint32_t(c.month) << 8) | uint32_t(c.day));
}

} // namespace

namespace snap {

std::string local_time_text() {
    const LocalClock c = read_clock();
    const int h12 = ((c.hour % 12) == 0) ? 12 : (c.hour % 12);
    char text[16];
    std::snprintf(text, sizeof text, "%d:%02d %s", h12, c.minute, (c.hour < 12) ? "AM" : "PM");
    return text;
}

void register_time_api_exports() {
    recomp::overlays::register_base_export("snap64_local_time", snap64_local_time);
    recomp::overlays::register_base_export("snap64_local_date", snap64_local_date);
    recomp::overlays::register_base_export("snap64_utc_offset", snap64_utc_offset);
}

} // namespace snap
