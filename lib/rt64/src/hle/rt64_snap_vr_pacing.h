#pragma once

#include <algorithm>
#include <cstdint>

namespace RT64 {
    // Keep fractional headset intervals across game frames. A 30 Hz game
    // cannot fit the 72 Hz display's 2/3-frame cadence into identical 33 ms
    // windows. Small overruns are paid back by the next window; a hitch must
    // never bank time for a burst of stale animation frames.
    class SnapVRPacing {
        int64_t deadline = 0;
        int64_t interval = 0;
        int64_t period = 0;
    public:
        void reset() { deadline = interval = period = 0; }

        void begin(int64_t now, int64_t sourceInterval, int64_t displayPeriod) {
            if (sourceInterval != interval || displayPeriod != period) reset();
            interval = std::max<int64_t>(sourceInterval, 1);
            period = std::max<int64_t>(displayPeriod, 1);
            deadline = deadline == 0 ? now + interval :
                std::clamp(deadline + interval, now, now + interval);
        }

        bool canRenderAnother(int64_t now) const {
            // A display sample may straddle the source deadline. Charge its
            // overrun to the next interval instead of discarding it up front.
            // Never start another sample after the deadline has been reached.
            return now < deadline;
        }
    };
}
