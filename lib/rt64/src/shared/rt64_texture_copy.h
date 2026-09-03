//
// RT64
//

#pragma once

#include "shared/rt64_hlsl.h"

#ifdef HLSL_CPU
namespace interop {
#endif
    struct TextureCopyCB {
        float2 uvScroll;
        float2 uvScale;
        // Pokemon Snap port: source texels averaged into each destination
        // pixel, per axis. {1,1} is the plain copy every other user wants.
        uint2 boxSize;
    };
#ifdef HLSL_CPU
};
#endif