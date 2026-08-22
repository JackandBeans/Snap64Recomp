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
        // Negative keeps the source texel's own alpha (the classic copy);
        // zero to one overrides it, for blended draws of one target over
        // another (the Pokemon Snap port's cut crossfade).
        float alpha;
        float padding;
    };
#ifdef HLSL_CPU
};
#endif