//
// RT64
//

#include "shared/rt64_other_mode.h"
#include "shared/rt64_raster_params.h"
#include "shared/rt64_render_flags.h"

#include "FbRendererCommon.hlsli"
#include "Library.hlsli"

[[vk::push_constant]] ConstantBuffer<RasterParams> gConstants : register(b0, space0);

LIBRARY_EXPORT void RasterVS(const RenderParams rp, in float4 iPosition, in float2 iUV, in float4 iColor, out float4 oPosition, out float2 oUV, out float4 oSmoothColor, out float4 oFlatColor) {
    float4 ndcPos = iPosition;
    
    // Skip any sort of transformation on the coordinates when rendering rects.
    if (!renderFlagRect(rp.flags)) {
        ndcPos.xy -= float2(FbParams.resolution / 2);
        ndcPos.xy /= float2(FbParams.resolution.x / 2.0f, FbParams.resolution.y / -2.0f);
        ndcPos.xyz *= ndcPos.w;
    }
    
    // Apply screen scale and offset.
    ndcPos.xy = (ndcPos.xy * gConstants.screenScale) + gConstants.screenOffset * ndcPos.w;
    
    // A draw that supplies its own depth (G_ZS_PRIM) is rasterised at the
    // near plane; the pixel shader writes the primitive depth itself.
    //
    // Pokemon Snap port: this used to multiply the primitive depth in here,
    // and on this build the value the vertex stage read back from the
    // per-call parameters was not the call's own: every particle of the
    // effect system landed near enough to pass the depth test against
    // any wall, while the same array read from the pixel stage was right
    // (the colour blend and the recolour depend on it every frame). The
    // pixel stage already writes the fragment depth for every draw
    // (SV_DepthGreaterEqual, quantised onto the console's grid), so it
    // now writes the primitive depth too; rasterising at the near plane
    // keeps its output no less than the interpolated value, which is
    // what that output semantic requires.
    const OtherMode otherMode = { rp.omL, rp.omH };
    const bool copyMode = (otherMode.cycleType() == G_CYC_COPY);
    const bool zSourcePrim = (otherMode.zSource() == G_ZS_PRIM);
    if (!copyMode && zSourcePrim) {
        ndcPos.z = 0.0f;
    }

    oPosition = ndcPos;
    oUV = iUV;
    oSmoothColor = iColor;
    oFlatColor = iColor;
}

#if defined(DYNAMIC_RENDER_PARAMS)
RenderParams getRenderParams() {
    uint instanceIndex = instanceRenderIndices[gConstants.renderIndex].instanceIndex;
    return DynamicRenderParams[instanceIndex];
}
#elif defined(SPEC_CONSTANT_RENDER_PARAMS)
#   include "RenderParamsSpecConstants.hlsli"
#endif

#if defined(DYNAMIC_RENDER_PARAMS) || defined(SPEC_CONSTANT_RENDER_PARAMS)
void VSMain(
    in float4 iPosition : POSITION
    , in float2 iUV : TEXCOORD
    , in float4 iColor : COLOR
    , out float4 oPosition : SV_POSITION
    , out float2 oUV : TEXCOORD
    , out float4 oSmoothColor : COLOR0
#if defined(DYNAMIC_RENDER_PARAMS) || defined(VERTEX_FLAT_COLOR)
    , out float4 oFlatColor : COLOR1
#endif
)
{
#if !defined(DYNAMIC_RENDER_PARAMS) && !defined(VERTEX_FLAT_COLOR)
    float4 oFlatColor;
#endif
    RasterVS(getRenderParams(), iPosition, iUV, iColor, oPosition, oUV, oSmoothColor, oFlatColor);
}
#endif