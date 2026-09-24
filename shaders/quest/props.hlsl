
struct Constants{row_major float4x4 vp;float4 deformation;};
[[vk::push_constant]] ConstantBuffer<Constants> constants;

[[vk::binding(0,0)]] Texture2D screenTex; [[vk::binding(1,0)]] Texture2D fluteTex; [[vk::binding(2,0)]] SamplerState sampler0;
struct V{float3 p:POSITION;float3 c:COLOR0;float3 uv:TEXCOORD;float alpha:COLOR1;};
struct P{float4 p:SV_POSITION;float3 c:COLOR0;float3 uv:TEXCOORD;float alpha:COLOR1;};
P vs(V v){P o;if(v.uv.z<0)v.p.y-=.0025*constants.deformation.x;o.p=mul(float4(v.p,1),constants.vp);o.p.z=(o.p.z+o.p.w)*0.5;o.c=v.c;o.uv=v.uv;o.alpha=v.alpha;return o;}
float4 ps(P p):SV_TARGET {
    if(p.uv.z>1.5) {
        float4 ink=fluteTex.Sample(sampler0,p.uv.xy);
        if(p.uv.z>2.5)ink.rgb=dot(ink.rgb,float3(.299,.587,.114))*.45;
        return float4(p.c*(1-ink.a)+ink.rgb,1);
    }
    return p.uv.z>.5?float4(screenTex.Sample(sampler0,p.uv.xy).rgb,1):float4(p.c,p.alpha);
}
