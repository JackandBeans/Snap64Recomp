#ifndef SNAP_WIDESCREEN_H
#define SNAP_WIDESCREEN_H
#include <algorithm>
#include <cmath>

namespace snap {
void update_visibility_viewport(int width, int height);

inline float visibility_expansion(int width, int height) {
    return height > 0 ? std::max(1.0f, float(width) / float(height) * 0.75f) : 1.0f;
}

// Retail camera-space rejection, with its truncation and vertical/depth limits.
// The extra horizontal margin follows the renderer's expanded aspect ratio.
inline bool outside_wide_view(float x, float y, float z, float expansion) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        z > -1.0f || z < -10000.0f) return true;
    const float px = std::trunc(x * 228.506134f / z);
    const float py = std::trunc(y * 228.506134f / z);
    return std::fabs(px) > 240.0f * expansion || std::fabs(py) > 180.0f;
}
}
#endif
