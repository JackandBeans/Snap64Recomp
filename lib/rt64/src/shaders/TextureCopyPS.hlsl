//
// RT64
//

#include "shared/rt64_texture_copy.h"

[[vk::push_constant]] ConstantBuffer<TextureCopyCB> gConstants : register(b0);

Texture2D<float4> gInput : register(t1);

float4 PSMain(in float4 pos : SV_Position, in float2 uv : TEXCOORD0) : SV_TARGET {
    // Pokemon Snap port: a box above one averages that many source texels per
    // destination pixel -- how a photo the game halved on the CPU is served
    // from the high-resolution render (hle/rt64_snap_photo_detail.h). At one
    // this is the copy it always was.
    if ((gConstants.boxSize.x <= 1) && (gConstants.boxSize.y <= 1)) {
        uint2 pixelPos = gConstants.uvScroll + uv.xy * gConstants.uvScale;
        return gInput.Load(uint3(pixelPos, 0));
    }

    // The viewport spans the destination, whose extent is the source extent
    // over the box; each destination pixel averages its own box of sources.
    const uint2 boxSize = max(gConstants.boxSize, uint2(1, 1));
    const uint2 dstPos = uint2(uv.xy * (gConstants.uvScale / float2(boxSize)));
    const uint2 basePos = uint2(gConstants.uvScroll) + dstPos * boxSize;
    float4 sum = float4(0.0f, 0.0f, 0.0f, 0.0f);
    for (uint y = 0; y < boxSize.y; y++) {
        for (uint x = 0; x < boxSize.x; x++) {
            sum += gInput.Load(uint3(basePos + uint2(x, y), 0));
        }
    }

    // Opaque, whatever the render's alpha where nothing was drawn: the
    // game's halving sets the low bit of every RGBA5551 pixel it writes, so
    // the sprite the console showed had no transparent texel, and a photo
    // drawn over the Gallery's backdrop must not let it through.
    const float4 average = sum / float(boxSize.x * boxSize.y);
    return float4(average.rgb, 1.0f);
}