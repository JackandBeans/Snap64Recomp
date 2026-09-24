[[vk::binding(0,0)]] Texture2D<float4> source;
float4 vs(uint id:SV_VertexID):SV_POSITION {
    float2 uv=float2((id<<1)&2,id&2);
    return float4(uv*float2(2,-2)+float2(-1,1),0,1);
}
float4 ps(float4 position:SV_POSITION):SV_TARGET {
    // One-to-one transfer through an UNORM view preserves gamma-encoded bytes.
    return source.Load(int3(int2(position.xy),0));
}
