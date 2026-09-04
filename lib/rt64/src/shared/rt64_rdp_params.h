//
// RT64
//

#pragma once

#include "rt64_hlsl.h"

#ifdef HLSL_CPU
namespace interop {
#endif
    struct RDPParams {
        float4 primColor;
        float2 primLOD;
        float2 primDepth;
        float4 envColor;
        float4 fogColor;
        float4 blendColor;
        float3 keyCenter;
        float3 keyScale;
        int convertK[6];
        // Pokemon Snap port: when snapPrimBlend is set, the primitive colour
        // this call had in the previous matched frame; the raster shader
        // blends from it to primColor by the sub-frame's weight
        // (hle/rt64_game_frame.cpp, GameFrame::snapMatchPrimColors).
        float4 snapPrevPrimColor;
        float snapPrimBlend;
        float snapPrimPad0;
        float snapPrimPad1;
        float snapPrimPad2;
    };
#ifdef HLSL_CPU
};
#endif