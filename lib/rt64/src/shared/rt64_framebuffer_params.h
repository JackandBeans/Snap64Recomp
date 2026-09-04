//
// RT64
//

#pragma once

#include "shared/rt64_hlsl.h"

#ifdef HLSL_CPU
namespace interop {
#endif
    struct FramebufferParams {
        float2 resolution;
        float2 resolutionScale;
        float horizontalMisalignment;
        // Pokemon Snap port: how far this image sits between the previous
        // matched frame and the current one; 1 on the frame the game drew.
        float snapPrimWeight;
    };
#ifdef HLSL_CPU
};
#endif