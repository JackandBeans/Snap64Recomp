
struct Constants{float4 origin;float4 right;float4 up;float4 forward;float4 tangents;float4 settings;};
[[vk::push_constant]] ConstantBuffer<Constants> constants;

struct P{float4 position:SV_POSITION;float2 uv:TEXCOORD;};
P vs(uint id:SV_VertexID){P p;p.uv=float2((id<<1)&2,id&2);p.position=float4(p.uv*float2(2,-2)+float2(-1,1),0,1);return p;}
float4 ps(P p):SV_TARGET {
    float visible=constants.settings.x;
    if(constants.settings.y>.5) {
        float3 ray=constants.right.xyz*lerp(constants.tangents.x,constants.tangents.y,p.uv.x)+constants.up.xyz*lerp(constants.tangents.z,constants.tangents.w,p.uv.y)+constants.forward.xyz;
        float distance=(-1.6-constants.origin.z)/min(ray.z,-.00001);
        float2 hit=(constants.origin.xyz+ray*distance).xy-float2(0,constants.settings.z);
        float edge=max(abs(hit.x)/.95,abs(hit.y)/.65);
        visible*=ray.z<0&&distance>0?1-smoothstep(.80,1,edge):0;
    }
    return float4(0,0,0,1-visible);
}
