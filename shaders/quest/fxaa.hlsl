#define FXAA_PC 1
#define FXAA_HLSL_5 1
#define FXAA_GREEN_AS_LUMA 1
#define FXAA_QUALITY__PRESET 12
#include "third_party/Fxaa3_11.h"

[[vk::binding(0,0)]] Texture2D<float4> source;
[[vk::binding(1,0)]] SamplerState linearClamp;
struct Constants { float2 reciprocalSize; float2 padding; };
[[vk::push_constant]] ConstantBuffer<Constants> parameters;
float4 vs(uint id:SV_VertexID):SV_POSITION {
    float2 uv=float2((id<<1)&2,id&2);
    return float4(uv*float2(2,-2)+float2(-1,1),0,1);
}
float4 ps(float4 position:SV_POSITION):SV_TARGET {
    // RT64 eye targets already contain perceptual/gamma-encoded RGB. Filter
    // those values and preserve them through the swapchain's UNORM view.
    FxaaTex texture={linearClamp,source};
    float2 uv=position.xy*parameters.reciprocalSize;
    float4 color=FxaaPixelShader(uv,0,texture,texture,texture,texture,
        parameters.reciprocalSize,0,0,0,.5f,.166f,.0312f,0,0,0,0);
    return float4(color.rgb,1);
}
