//
// RT64
//

#include "TextureDecoder.hlsli"

#define GROUP_SIZE 8

struct TextureDecodeCB {
    uint2 Resolution;
    uint fmt;
    uint siz;
    uint address;
    uint stride;
    uint tlut;
    uint palette;
    // Pokemon Snap port: which mip level this dispatch writes. Level 0 is the
    // decode the shader always did; deeper levels are generated here too.
    uint mipLevel;
    uint mipPad;
};

[[vk::push_constant]] ConstantBuffer<TextureDecodeCB> gConstants : register(b0);
Texture1D<uint> TMEM : register(t1);
RWTexture2D<float4> RGBA32 : register(u2);

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void CSMain(uint2 coord : SV_DispatchThreadID) {
    if ((coord.x >= gConstants.Resolution.x) || (coord.y >= gConstants.Resolution.y)) {
        return;
    }

    if (gConstants.mipLevel == 0) {
        RGBA32[coord] = sampleTMEM(coord, gConstants.siz, gConstants.fmt, gConstants.address, gConstants.stride, gConstants.tlut, gConstants.palette, TMEM);
        return;
    }

    // Pokemon Snap port: every level is a true box over the ORIGINAL TMEM
    // texels rather than a filter chain over the level above it -- no error
    // accumulates, no level has to be read back, and no barrier is needed
    // between levels because each writes a disjoint subresource.
    const int span = 1 << gConstants.mipLevel;
    const int2 base = int2(coord) * span;
    float3 rgbWeighted = float3(0.0f, 0.0f, 0.0f);
    float3 rgbFlat = float3(0.0f, 0.0f, 0.0f);
    float alphaSum = 0.0f;
    for (int y = 0; y < span; y++) {
        for (int x = 0; x < span; x++) {
            const float4 texel = sampleTMEM(base + int2(x, y), gConstants.siz, gConstants.fmt, gConstants.address, gConstants.stride, gConstants.tlut, gConstants.palette, TMEM);
            rgbWeighted += texel.rgb * texel.a;
            rgbFlat += texel.rgb;
            alphaSum += texel.a;
        }
    }

    // Alpha-weighted colour. This art leans on cutout alpha, where the colour
    // beneath a transparent texel is arbitrary; a flat average would bleed
    // that unseen colour into the visible edge as the level shrinks.
    const float count = float(span * span);
    const float3 rgb = (alphaSum > 0.0f) ? (rgbWeighted / alphaSum) : (rgbFlat / count);
    RGBA32[coord] = float4(rgb, alphaSum / count);
}