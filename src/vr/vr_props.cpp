#include "vr_props.h"
#include "vr_font.h"
#include "vr_menu.h"
#include "vr_messages.h"
#include "vr_transparency.h"
#include <cstdio>
#include <chrono>
#include "paths.h"
#ifndef __ANDROID__
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#endif
#include <json/json.hpp>
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <cstddef>
#include <unordered_map>
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>
#ifndef __ANDROID__
using Microsoft::WRL::ComPtr;
#endif
namespace snap::vr {
namespace {
#ifndef __ANDROID__
void ok(HRESULT h){if(FAILED(h))throw std::runtime_error("VR D3D12 failure: "+std::to_string(h));}
#endif
struct Vertex {Vec3 position;float r,g,b,u=0,v=0,textured=0,alpha=1;};
struct Color {float r,g,b;};
void triangle(std::vector<Vertex>& out,Pose p,Vec3 a,Vec3 b,Vec3 c,Color color) {
    Vec3 n=normalized(cross(b-a,c-a));float shade=std::clamp(.75f+dot(n,{.1f,.3f,.2f}),.35f,1.0f);
    for(auto v:{a,b,c})out.push_back({p.position+rotate(p.orientation,v),color.r*shade,color.g*shade,color.b*shade});
}
void box(std::vector<Vertex>& out,Pose p,Vec3 center,Vec3 half,Color color) {
    std::array<Vec3,8> v;for(int i=0;i<8;i++)v[i]=center+Vec3{(i&1)?half.x:-half.x,(i&2)?half.y:-half.y,(i&4)?half.z:-half.z};
    const int faces[][4]={{0,4,6,2},{1,3,7,5},{0,1,5,4},{2,6,7,3},{0,2,3,1},{4,5,7,6}};
    for(auto& f:faces){triangle(out,p,v[f[0]],v[f[1]],v[f[2]],color);triangle(out,p,v[f[0]],v[f[2]],v[f[3]],color);}
}
void screenQuad(std::vector<Vertex>& out,Pose p,float width,float height,float texture=1,Color tint={1,1,1}) {
    const float coords[][4]={{-1,-1,0,1},{-1,1,0,0},{1,1,1,0},{-1,-1,0,1},{1,1,1,0},{1,-1,1,1}};
    for(auto c:coords)out.push_back({p.position+rotate(p.orientation,{c[0]*width/2,c[1]*height/2,0}),tint.r,tint.g,tint.b,c[2],c[3],texture});
}
void fluteCap(std::vector<Vertex>& out,Pose cap,Color color) {
    constexpr unsigned segments=48;
    auto point=[](float angle,float radius,float y){return Vec3{std::cos(angle)*radius,y,std::sin(angle)*radius};};
    for(unsigned i=0;i<segments;i++) {
        float a=2*pi*i/segments,b=2*pi*(i+1)/segments;
        Vec3 topA=point(a,.0455f,0),topB=point(b,.0455f,0);
        triangle(out,cap,{},topB,topA,color);
        for(auto band:std::array<std::array<float,4>,2>{{{.0455f,0,.047f,-.003f},{.047f,-.003f,.047f,-.010f}}}) {
            auto p=point(a,band[0],band[1]),q=point(b,band[0],band[1]);
            auto r=point(b,band[2],band[3]),s=point(a,band[2],band[3]);
            triangle(out,cap,p,q,r,color);triangle(out,cap,p,r,s,color);
        }
    }
}
void label(std::vector<Vertex>& out,Pose panel,const char* text,float x,float y,float pixel,Color color) {
    for(const char* c=text;*c;c++,x+=pixel*6) {
        auto bits=glyph(*c);
        for(int column=0;column<5;column++)for(int row=0;row<7;row++)if(bits[column]&(1<<row)) {
            Vec3 p{x+column*pixel,y-row*pixel,.005f};
            triangle(out,panel,p,p+Vec3{0,-pixel,0},p+Vec3{pixel,-pixel,0},color);
            triangle(out,panel,p,p+Vec3{pixel,-pixel,0},p+Vec3{pixel,0,0},color);
        }
    }
}
Matrix multiply(const Matrix&a,const Matrix&b){Matrix c{};for(int i=0;i<4;i++)for(int j=0;j<4;j++)for(int k=0;k<4;k++)c[i*4+j]+=a[i*4+k]*b[k*4+j];return c;}
Quat mix(Quat a,Quat b,float t){float d=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;if(d<0){b.x=-b.x;b.y=-b.y;b.z=-b.z;b.w=-b.w;}Quat q{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t};float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);return {q.x/n,q.y/n,q.z/n,q.w/n};}
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<bool> shutter;
    void load(const nlohmann::json& j) {
        const auto data=j.at("vertices").get<std::vector<float>>();
        const auto indices=j.at("indices").get<std::vector<unsigned>>();
        const auto alpha=j.value("alpha",std::vector<float>{});
        if(data.size()%11||indices.size()%3)throw std::runtime_error("Invalid VR model layout");
        if(!alpha.empty()&&alpha.size()!=data.size()/11)throw std::runtime_error("Invalid VR model alpha layout");
        vertices.reserve(indices.size());
        auto shutterIndices=j.value("shutter_vertices",std::vector<unsigned>{});
        for(unsigned index:indices) {
            shutter.push_back(std::find(shutterIndices.begin(),shutterIndices.end(),index)!=shutterIndices.end());
            size_t i=size_t(index)*11;if(i+10>=data.size())throw std::runtime_error("VR model index out of range");
            Vec3 n{data[i+3],data[i+4],data[i+5]};
            float shade=std::clamp(.80f+dot(n,{.12f,.25f,.18f}),.35f,1.f);
            float opacity=alpha.empty()?1.f:alpha[index];
            if(!std::isfinite(opacity)||opacity<0||opacity>1)throw std::runtime_error("Invalid VR model opacity");
            vertices.push_back({{data[i],data[i+1],data[i+2]},data[i+6]*shade,data[i+7]*shade,data[i+8]*shade,0,0,0,opacity});
        }
    }
    void draw(std::vector<Vertex>& out,Pose pose,bool mirrorX=false,float press=0) const {
        out.reserve(out.size()+vertices.size());
        for(size_t i=0;i<vertices.size();i++) {
            // Reflection reverses winding; swap the last two triangle corners.
            size_t index=mirrorX?(i/3*3+(i%3==0?0:3-i%3)):i;
            auto vertex=vertices[index];if(shutter[index])vertex.position.y-=.0025f*press;
            if(mirrorX)vertex.position.x=-vertex.position.x;
            vertex.position=pose.position+rotate(pose.orientation,vertex.position);out.push_back(vertex);
        }
    }
};
struct Rig {
    std::vector<float> rest,ibm,verts,open,fist,cameraGrip,itemGrip;
    std::vector<int> parent,tris;
    std::vector<std::string> names;
    void load(const nlohmann::json& j){rest=j.at("rest").get<std::vector<float>>();ibm=j.at("ibm").get<std::vector<float>>();verts=j.at("verts").get<std::vector<float>>();parent=j.at("parent").get<std::vector<int>>();tris=j.at("tris").get<std::vector<int>>();names=j.at("bones").get<std::vector<std::string>>();open=j.at("poses").at("open").get<std::vector<float>>();fist=j.at("poses").at("fist").get<std::vector<float>>();cameraGrip=j.at("poses").at("grip_1").get<std::vector<float>>();itemGrip=j.at("poses").at("grip_4").get<std::vector<float>>();}
    void draw(std::vector<Vertex>& out,Pose wrist,const HandInput& input,Held held,const Vec3* shutterTarget=nullptr) const {
        std::vector<Pose> world(parent.size());
        for(size_t i=0;i<parent.size();i++) {
            float t=input.squeeze;
            if(names[i].starts_with("Index"))t=held==Held::Camera?0.f:input.trigger;
            if(names[i].starts_with("Thumb"))t=input.thumbTouch?.7f:.15f;
            if(held!=Held::None&&!names[i].starts_with("Index"))t=std::max(t,held==Held::Camera?.55f:.65f);
            // DramaticShape's ball hold is a full static pose. Trigger and
            // thumb touches must not uncurl fingers through the held item.
            if(held==Held::Apple||held==Held::PesterBall)t=1.f;
            const auto& target=held==Held::Camera?cameraGrip:held!=Held::None?itemGrip:fist;
            size_t q=i*4;Pose p{mix({open[q],open[q+1],open[q+2],open[q+3]},{target[q],target[q+1],target[q+2],target[q+3]},t),{rest[i*7],rest[i*7+1],rest[i*7+2]}};
            world[i]=parent[i]?compose(world[parent[i]-1],p):p;
        }
        if(shutterTarget) {
            // Solve only the index chain against the physical button. The
            // remaining fingers retain the imported camera grip pose.
            Vec3 target=rotate(conjugate(wrist.orientation),*shutterTarget-wrist.position);
            std::array<size_t,5> chain{};size_t count=0;
            for(size_t i=0;i<names.size();i++)if(names[i].starts_with("Index")&&count<chain.size())chain[count++]=i;
            if(count==chain.size())for(int iteration=0;iteration<24;iteration++) {
                for(int joint=3;joint>=0;joint--) {
                    size_t root=chain[joint];Vec3 origin=world[root].position;
                    Vec3 a=normalized(world[chain[4]].position-origin),b=normalized(target-origin);
                    float cosine=std::clamp(dot(a,b),-1.f,1.f);
                    Vec3 axis=cross(a,b);Quat turn{axis.x,axis.y,axis.z,1+cosine};
                    float norm=std::sqrt(dot(axis,axis)+turn.w*turn.w);
                    if(norm<1e-6f)continue;
                    turn={turn.x/norm,turn.y/norm,turn.z/norm,turn.w/norm};
                    for(int k=joint;k<5;k++) {
                        auto& pose=world[chain[k]];
                        pose.position=origin+rotate(turn,pose.position-origin);pose.orientation=turn*pose.orientation;
                    }
                }
                if(length(world[chain[4]].position-target)<.0002f)break;
            }
        }
        std::vector<Vertex> transformed;transformed.reserve(verts.size()/15);
        for(size_t i=0;i<verts.size();i+=15) {
            Vec3 p{},n{};
            for(int k=0;k<4;k++) {
                float weight=verts[i+11+k];if(weight<=0)continue;size_t b=size_t(verts[i+7+k])-1;const float* m=&ibm[b*12];
                Vec3 v{m[0]*verts[i]+m[1]*verts[i+1]+m[2]*verts[i+2]+m[3],m[4]*verts[i]+m[5]*verts[i+1]+m[6]*verts[i+2]+m[7],m[8]*verts[i]+m[9]*verts[i+1]+m[10]*verts[i+2]+m[11]};
                Vec3 norm{m[0]*verts[i+3]+m[1]*verts[i+4]+m[2]*verts[i+5],m[4]*verts[i+3]+m[5]*verts[i+4]+m[6]*verts[i+5],m[8]*verts[i+3]+m[9]*verts[i+4]+m[10]*verts[i+5]};
                p=p+(world[b].position+rotate(world[b].orientation,v))*weight;n=n+rotate(world[b].orientation,norm)*weight;
            }
            float u=verts[i+6];Color color=u<.25f?Color{.91f,.65f,.43f}:u<.5f?Color{.10f,.35f,.18f}:u<.75f?Color{.08f,.1f,.08f}:Color{.65f,.73f,.56f};
            float shade=std::clamp(.775f+dot(normalized(n),{.06f,.225f,.05f}),.3f,1.f);
            transformed.push_back({wrist.position+rotate(wrist.orientation,p),color.r*shade,color.g*shade,color.b*shade});
        }
        for(int i:tris)out.push_back(transformed.at(i));
    }
};
const char* shader=R"(
cbuffer Constants:register(b0){row_major float4x4 vp;}
Texture2D screenTex:register(t0);Texture2D fluteTex:register(t1);SamplerState sampler0:register(s0);
struct V{float3 p:POSITION;float3 c:COLOR0;float3 uv:TEXCOORD;float alpha:COLOR1;};
struct P{float4 p:SV_POSITION;float3 c:COLOR0;float3 uv:TEXCOORD;float alpha:COLOR1;};
P vs(V v){P o;o.p=mul(float4(v.p,1),vp);o.p.z=(o.p.z+o.p.w)*0.5;o.c=v.c;o.uv=v.uv;o.alpha=v.alpha;return o;}
float4 ps(P p):SV_TARGET {
    if(p.uv.z>1.5) {
        float4 ink=fluteTex.Sample(sampler0,p.uv.xy);
        if(p.uv.z>2.5)ink.rgb=dot(ink.rgb,float3(.299,.587,.114))*.45;
        return float4(p.c*(1-ink.a)+ink.rgb,1);
    }
    return p.uv.z>.5?float4(screenTex.Sample(sampler0,p.uv.xy).rgb,1):float4(p.c,p.alpha);
}
)";
const char* presentationShader=R"(
cbuffer Constants:register(b0){float4 origin;float4 right;float4 up;float4 forward;float4 tangents;float4 settings;}
struct P{float4 position:SV_POSITION;float2 uv:TEXCOORD;};
P vs(uint id:SV_VertexID){P p;p.uv=float2((id<<1)&2,id&2);p.position=float4(p.uv*float2(2,-2)+float2(-1,1),0,1);return p;}
float4 ps(P p):SV_TARGET {
    float visible=settings.x;
    if(settings.y>.5) {
        float3 ray=right.xyz*lerp(tangents.x,tangents.y,p.uv.x)+up.xyz*lerp(tangents.z,tangents.w,p.uv.y)+forward.xyz;
        float distance=(-1.6-origin.z)/min(ray.z,-.00001);
        float2 hit=(origin.xyz+ray*distance).xy-float2(0,settings.z);
        float edge=max(abs(hit.x)/.95,abs(hit.y)/.65);
        visible*=ray.z<0&&distance>0?1-smoothstep(.80,1,edge):0;
    }
    return float4(0,0,0,1-visible);
}
)";
}
#ifdef __ANDROID__
#include "vr_props_vulkan_impl.inl"
#else
struct Props::Impl {
    ID3D12Device* device;ID3D12CommandQueue* queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline,transparentPipeline,presentationPipeline;
    ComPtr<ID3D12RootSignature> presentationRoot;
    ComPtr<ID3D12DescriptorHeap> rtv,dsv,srv;
    ComPtr<ID3D12Resource> vertices,indices,timestampReadback;
    ComPtr<ID3D12Resource> fluteTexture;
    ComPtr<ID3D12QueryHeap> timestampHeap;
    UINT64 timestampFrequency=0;
    double waitMs=0;
    UINT64 fenceValue=0;HANDLE event=nullptr;size_t capacity=0,indexCapacity=0;
    std::array<Rig,2> hands;
    Mesh cameraMesh,vehicleMesh,appleMesh,pesterBallMesh;
    std::vector<Vertex> frameVertices;
    std::array<std::vector<Vertex>,2> contactVertices;
    std::array<bool,2> contactReady{};
    uint64_t contactFrame=0;
    std::vector<unsigned> opaqueIndices;
    std::vector<TransparentTriangle> transparentTriangles;
    uint64_t geometryFrame=0;bool geometryReady=false;
    double flutePressTime=-100;
    Impl(ID3D12Device* d,ID3D12CommandQueue* q):device(d),queue(q) {
        D3D12_QUERY_HEAP_DESC query{};query.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;query.Count=2;
        ok(d->CreateQueryHeap(&query,IID_PPV_ARGS(&timestampHeap)));ok(q->GetTimestampFrequency(&timestampFrequency));
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=16;buffer.Height=1;
        buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ok(d->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&timestampReadback)));
        ok(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
        ok(d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));ok(list->Close());
        ok(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("VR fence event");
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.NumDescriptors=1;hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;ok(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv)));
        hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;ok(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&dsv)));
        hd.NumDescriptors=2;hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;ok(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&srv)));
        D3D12_SHADER_RESOURCE_VIEW_DESC empty{};empty.Format=DXGI_FORMAT_R8G8B8A8_UNORM;empty.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;empty.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;empty.Texture2D.MipLevels=1;
        auto iconHandle=srv->GetCPUDescriptorHandleForHeapStart();iconHandle.ptr+=d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);d->CreateShaderResourceView(nullptr,&empty,iconHandle);
        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,2,0,0,0};
        D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[0].Constants={0,0,16};params[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
        params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[1].DescriptorTable={1,&range};params[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
        D3D12_ROOT_SIGNATURE_DESC rd{2,params,1,&sampler,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};ComPtr<ID3DBlob> blob,errors;
        ok(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors));ok(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
        ComPtr<ID3DBlob> vs,ps;ok(D3DCompile(shader,std::strlen(shader),"snap_vr",nullptr,nullptr,"vs","vs_5_0",0,0,&vs,&errors));ok(D3DCompile(shader,std::strlen(shader),"snap_vr",nullptr,nullptr,"ps","ps_5_0",0,0,&ps,&errors));
        D3D12_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"COLOR",1,DXGI_FORMAT_R32_FLOAT,0,UINT(offsetof(Vertex,alpha)),D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.VS={vs->GetBufferPointer(),vs->GetBufferSize()};pd.PS={ps->GetBufferPointer(),ps->GetBufferSize()};pd.InputLayout={elements,4};
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;pd.SampleMask=UINT_MAX;pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pd.RasterizerState.DepthClipEnable=TRUE;
        pd.DepthStencilState.DepthEnable=TRUE;pd.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;pd.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
        pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pd.NumRenderTargets=1;pd.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;pd.DSVFormat=DXGI_FORMAT_D32_FLOAT;pd.SampleDesc.Count=1;
        ok(d->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&pipeline)));
        auto& blend=pd.BlendState.RenderTarget[0];blend.BlendEnable=TRUE;
        blend.SrcBlend=D3D12_BLEND_SRC_ALPHA;blend.DestBlend=D3D12_BLEND_INV_SRC_ALPHA;blend.BlendOp=D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha=D3D12_BLEND_ONE;blend.DestBlendAlpha=D3D12_BLEND_INV_SRC_ALPHA;blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;
        pd.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;
        ok(d->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&transparentPipeline)));
        D3D12_ROOT_PARAMETER postParam{};postParam.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;postParam.Constants={0,0,24};
        D3D12_ROOT_SIGNATURE_DESC postDesc{1,&postParam,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ok(D3D12SerializeRootSignature(&postDesc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors));
        ok(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&presentationRoot)));
        ok(D3DCompile(presentationShader,std::strlen(presentationShader),"vr_presentation",nullptr,nullptr,"vs","vs_5_0",0,0,&vs,&errors));
        ok(D3DCompile(presentationShader,std::strlen(presentationShader),"vr_presentation",nullptr,nullptr,"ps","ps_5_0",0,0,&ps,&errors));
        pd.pRootSignature=presentationRoot.Get();pd.VS={vs->GetBufferPointer(),vs->GetBufferSize()};pd.PS={ps->GetBufferPointer(),ps->GetBufferSize()};pd.InputLayout={};
        pd.DepthStencilState.DepthEnable=FALSE;pd.DSVFormat=DXGI_FORMAT_UNKNOWN;
        ok(d->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&presentationPipeline)));
        std::ifstream file(snap::base_dir()/"assets/vr/hands.json");if(!file)throw std::runtime_error("Missing assets/vr/hands.json");nlohmann::json j;file>>j;hands[0].load(j.at("left"));hands[1].load(j.at("right"));
        std::ifstream models(snap::base_dir()/"assets/vr/props.json");if(!models)throw std::runtime_error("Missing assets/vr/props.json");
        nlohmann::json props;models>>props;cameraMesh.load(props.at("models").at("camera"));vehicleMesh.load(props.at("models").at("zero_one"));
        appleMesh.load(props.at("models").at("apple"));pesterBallMesh.load(props.at("models").at("pester_ball"));
    }
    ~Impl(){if(event)CloseHandle(event);}
    void begin(){ok(allocator->Reset());ok(list->Reset(allocator.Get(),nullptr));}
    void finish(){ok(list->Close());ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);ok(queue->Signal(fence.Get(),++fenceValue));ok(fence->SetEventOnCompletion(fenceValue,event));auto start=std::chrono::steady_clock::now();if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0)throw std::runtime_error("VR GPU fence timed out");waitMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
    void uploadFluteIcon(const FluteIcon& icon) {
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width=desc.Height=FluteIcon::side;desc.DepthOrArraySize=desc.MipLevels=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        ok(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&fluteTexture)));
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;
        device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&bytes);
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=bytes;
        buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        heap.Type=D3D12_HEAP_TYPE_UPLOAD;ComPtr<ID3D12Resource> upload;
        ok(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)));
        uint8_t* pixels;D3D12_RANGE read{0,0};ok(upload->Map(0,&read,reinterpret_cast<void**>(&pixels)));
        for(unsigned y=0;y<FluteIcon::side;y++)for(unsigned x=0;x<FluteIcon::side;x++) {
            const auto* src=&icon.rgba[(y*FluteIcon::side+x)*4];auto* dst=pixels+y*footprint.Footprint.RowPitch+x*4;
            // Premultiply before filtering: transparent source texels are yellow.
            for(unsigned channel=0;channel<3;channel++)dst[channel]=uint8_t((unsigned(src[channel])*src[3]+127)/255);
            dst[3]=src[3];
        }
        upload->Unmap(0,nullptr);begin();
        D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=footprint;
        D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=fluteTexture.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={fluteTexture.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};list->ResourceBarrier(1,&barrier);finish();
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=desc.Format;view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Texture2D.MipLevels=1;
        auto handle=srv->GetCPUDescriptorHandleForHeapStart();handle.ptr+=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        device->CreateShaderResourceView(fluteTexture.Get(),&view,handle);
    }
};
Props::Props(ID3D12Device*d,ID3D12CommandQueue*q):impl(std::make_unique<Impl>(d,q)){}
Props::~Props()=default;
#endif
std::array<FluteContact,2> Props::handContacts(const Tracking& tracking,const Interaction& interaction) {
    auto& x=*impl;std::array<FluteContact,2> contacts{};
    x.contactReady={};x.contactFrame=tracking.frame;
    if(!tracking.focused||!tracking.headValid)return contacts;
    for(unsigned hand=0;hand<2;hand++) {
        const auto& input=tracking.hands[hand];if(!input.tracked)continue;
        Pose wrist=handMeshPose(interaction.localPose(input.grip));
        if(length(wrist.position-Interaction::fluteButton)>.45f)continue;
        // Use the same skinning and empty-hand pose as the rendered hands. The
        // controller grip origin can be many centimeters above a touching finger.
        auto& vertices=x.contactVertices[hand];vertices.clear();
        x.hands[hand].draw(vertices,wrist,input,Held::None);
        x.contactReady[hand]=true;
        auto& contact=contacts[hand];
        for(const auto& vertex:vertices)contact.include(vertex.position,Interaction::fluteButton);
        // An edge can cross the cap between skin vertices during a downward push.
        for(size_t i=0;i<vertices.size();i+=3)for(unsigned edge=0;edge<3;edge++) {
            Vec3 a=vertices[i+edge].position,b=vertices[i+(edge+1)%3].position;
            float ay=a.y-Interaction::fluteButton.y,by=b.y-Interaction::fluteButton.y;
            if(ay*by<0)contact.include(a+(b-a)*(ay/(ay-by)),Interaction::fluteButton);
        }
    }
    return contacts;
}
#ifdef __ANDROID__
#include "vr_props_vulkan_commands.inl"
#else
void Props::presentation(ID3D12Resource* color,Pose eye,Fov fov,float gain,bool portal,float height) {
    auto& x=*impl;
    Vec3 r=rotate(eye.orientation,{1,0,0}),u=rotate(eye.orientation,{0,1,0}),f=rotate(eye.orientation,{0,0,-1});
    float constants[]={eye.position.x,eye.position.y,eye.position.z,0,r.x,r.y,r.z,0,u.x,u.y,u.z,0,f.x,f.y,f.z,0,
        std::tan(fov.left),std::tan(fov.right),std::tan(fov.up),std::tan(fov.down),gain,portal?1.f:0.f,height,0};
    x.device->CreateRenderTargetView(color,nullptr,x.rtv->GetCPUDescriptorHandleForHeapStart());
    x.begin();auto rt=x.rtv->GetCPUDescriptorHandleForHeapStart();x.list->OMSetRenderTargets(1,&rt,FALSE,nullptr);
    auto desc=color->GetDesc();D3D12_VIEWPORT vp{0,0,float(desc.Width),float(desc.Height),0,1};D3D12_RECT rect{0,0,LONG(desc.Width),LONG(desc.Height)};
    x.list->RSSetViewports(1,&vp);x.list->RSSetScissorRects(1,&rect);x.list->SetPipelineState(x.presentationPipeline.Get());
    x.list->SetGraphicsRootSignature(x.presentationRoot.Get());x.list->SetGraphicsRoot32BitConstants(0,24,constants,0);
    x.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);x.list->DrawInstanced(3,1,0,0);x.finish();
}
void Props::beginTiming(){auto& x=*impl;x.waitMs=0;x.begin();x.list->EndQuery(x.timestampHeap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);x.finish();}
double Props::endTiming(){
    auto& x=*impl;x.begin();x.list->EndQuery(x.timestampHeap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,1);
    x.list->ResolveQueryData(x.timestampHeap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,2,x.timestampReadback.Get(),0);x.finish();
    UINT64* data=nullptr;D3D12_RANGE range{0,16};ok(x.timestampReadback->Map(0,&range,reinterpret_cast<void**>(&data)));
    double elapsed=1000.0*double(data[1]-data[0])/double(x.timestampFrequency);x.timestampReadback->Unmap(0,nullptr);return elapsed;
}
double Props::fenceWaitMs()const{return impl->waitMs;}

void Props::capture(ID3D12Resource*source,const char* filename) {
    auto& x=*impl;auto desc=source->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;UINT rows;UINT64 rowSize,size;
    x.device->GetCopyableFootprints(&desc,0,1,0,&footprint,&rows,&rowSize,&size);
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=size;buffer.Height=1;buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;ok(x.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
    x.begin();D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={source,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};x.list->ResourceBarrier(1,&barrier);
    D3D12_TEXTURE_COPY_LOCATION s{source,D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,{}},d{};d.pResource=readback.Get();d.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;d.PlacedFootprint=footprint;x.list->CopyTextureRegion(&d,0,0,0,&s,nullptr);
    std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);x.list->ResourceBarrier(1,&barrier);x.finish();
    void* data;D3D12_RANGE range{0,SIZE_T(size)};ok(readback->Map(0,&range,&data));
    if(!stbi_write_png(filename,int(desc.Width),int(desc.Height),4,data,int(footprint.Footprint.RowPitch))) {readback->Unmap(0,nullptr);throw std::runtime_error("VR capture write failed");}
    readback->Unmap(0,nullptr);
}
void Props::copy(ID3D12Resource*source,ID3D12Resource*dest,unsigned width,unsigned height) {
    auto& x=*impl;x.begin();D3D12_RESOURCE_BARRIER b[2]{};
    for(auto& v:b){v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;v.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;}
    b[0].Transition={source,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};
    // XR_KHR_D3D12_enable requires acquired color images to enter and
    // leave application use in RENDER_TARGET, not COMMON/PRESENT.
    b[1].Transition={dest,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_DEST};x.list->ResourceBarrier(2,b);
    D3D12_TEXTURE_COPY_LOCATION s{source,D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,{}},d{dest,D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,{}};
    D3D12_BOX box{0,0,0,width,height,1};x.list->CopyTextureRegion(&d,0,0,0,&s,&box);
    std::swap(b[0].Transition.StateBefore,b[0].Transition.StateAfter);std::swap(b[1].Transition.StateBefore,b[1].Transition.StateAfter);x.list->ResourceBarrier(2,b);x.finish();
}
#endif
void Props::draw(VRTexture*color,VRTexture*depth,VRTexture*screen,Pose eye,Fov fov,const GameState&g,const InteractionFrame&f,const Tracking&t,const Interaction&interaction,bool focus,bool optionsOpen,int optionsRow) {
    auto& x=*impl;auto& v=x.frameVertices;float unit=interaction.settings.unitsPerMeter;
    if(g.fluteIcon&&!x.fluteTexture)x.uploadFluteIcon(*g.fluteIcon);
    bool rebuild=!x.geometryReady||x.geometryFrame!=t.frame;
    // Work in meters for props and transform the game-space eye into meters.
    auto meters=[&](Pose p){p.position=p.position*(1/unit);return p;};
    Pose cart=meters(interaction.cartPose(g));
    if(rebuild){v.clear();x.geometryFrame=t.frame;x.geometryReady=true;
#ifdef __ANDROID__
    x.rigidDraws.clear();
#endif
    auto drawMesh=[&](const Mesh& mesh,Pose pose,bool mirror=false,float press=0.f) {
#ifdef __ANDROID__
        // Transparent pester balls retain their per-eye triangle sorting.
        if(&mesh!=&x.pesterBallMesh){x.rigidDraws.push_back({&mesh,pose,mirror,press});return;}
#endif
        mesh.draw(v,pose,mirror,press);
    };
    if(g.course) {
        for(const auto& puff:g.smoke) {
            float age=float(g.frame-puff.born)/30.f;
            if(age<0||age>1.2f)continue;
            for(int lobe=0;lobe<7;lobe++) {
                float angle=lobe*2.39996f,spread=.08f+age*.20f;
                Vec3 center=puff.position*(1/unit)+Vec3{std::cos(angle)*spread,age*.23f+.03f*(lobe%3),std::sin(angle)*spread};
                float radius=.08f+age*.18f;size_t begin=v.size();
                auto point=[&](int a,int b){float lat=-pi/2+a*pi/8,lon=b*2*pi/12;return Vec3{std::cos(lat)*std::cos(lon),std::sin(lat),std::cos(lat)*std::sin(lon)}*radius;};
                for(int a=0;a<8;a++)for(int b=0;b<12;b++) {
                    triangle(v,Pose{{},center},point(a,b),point(a+1,b),point(a+1,b+1),{.66f,.22f,.86f});
                    triangle(v,Pose{{},center},point(a,b),point(a+1,b+1),point(a,b+1),{.66f,.22f,.86f});
                }
                for(size_t n=begin;n<v.size();n++)v[n].alpha=.23f*(1-age/1.2f);
            }
        }
        drawMesh(x.vehicleMesh,cart);
        // Shallow round control seated in the dashboard's molded left lobe.
        const Vec3 button=Interaction::fluteButton;
        if(f.fluteTouch)x.flutePressTime=t.seconds;
        float travel=.003f*std::clamp(float(1-(t.seconds-x.flutePressTime)/.18),0.f,1.f);
        Color capColor=g.fluteUnlocked?Color{.90f,.91f,.86f}:Color{.42f,.45f,.46f};
        fluteCap(v,compose(cart,Pose{{},button+Vec3{0,-travel,0}}),capColor);
        if(x.fluteTexture) {
            Pose face=compose(cart,Pose{{-.70710678f,0,0,.70710678f},button+Vec3{0,.00025f-travel,0}});
            screenQuad(v,face,.064f,.064f,g.fluteUnlocked?2.f:3.f,capColor);
        }
        for(unsigned segment=0;segment<48;segment++) {
            float a=2*pi*segment/48-pi/2,b=2*pi*(segment+1)/48-pi/2;
            auto point=[&](float angle,float radius){return Vec3{button.x+std::cos(angle)*radius,.7752f,button.z+std::sin(angle)*radius};};
            bool playing=g.fluteSeconds>0&&segment<48*std::clamp(g.fluteSeconds/10.f,0.f,1.f);
            Color light=!g.fluteUnlocked?Color{.10f,.12f,.12f}:playing?Color{.12f,.72f,.32f}:Color{.32f,.26f,.12f};
            triangle(v,cart,point(a,.051f),point(b,.054f),point(a,.054f),light);
            triangle(v,cart,point(a,.051f),point(b,.051f),point(b,.054f),light);
        }
        for(int i=0;i<2;i++) {
            Vec3 p=i?Interaction::pesterBin:Interaction::appleBin;

            if(i?g.pesterBalls:g.apples)drawMesh(i?x.pesterBallMesh:x.appleMesh,compose(cart,Pose{{},p}));
        }

        const bool leftCamera=f.held[0]==Held::Camera;
        const int holdingHand=leftCamera?0:f.held[1]==Held::Camera?1:-1;
        Pose camera=holdingHand>=0?compose(meters(f.lens),Pose{{},{0,0,.14f}}):compose(cart,Pose{{},Interaction::cameraDock});
        const float press=holdingHand>=0?std::clamp(t.hands[holdingHand].trigger,0.f,1.f):0.f;
        // Fingertip is above the cap when released, then follows its 2.5 mm
        // travel on press. Pad clearance keeps the skin outside the casting.
        Vec3 shutterPoint=camera.position+rotate(camera.orientation,{leftCamera?-.064f:.064f,.0705f-.0065f*press,-.006f});
        for(int i=0;i<2;i++) {
            if(!t.hands[i].tracked)continue;
            Pose hand=meters(f.hands[i]);
            bool itemHeld=f.held[i]==Held::Apple||f.held[i]==Held::PesterBall;
            if(f.held[i]==Held::None&&x.contactReady[i]&&x.contactFrame==t.frame) {
                // Render exactly the surface that touched the dashboard.
                for(auto vertex:x.contactVertices[i]) {
                    vertex.position=cart.position+rotate(cart.orientation,vertex.position);v.push_back(vertex);
                }
            }else x.hands[i].draw(v,itemHeld?itemHandPose(hand,i):handMeshPose(hand),t.hands[i],f.held[i],i==holdingHand?&shutterPoint:nullptr);
            if(f.held[i]!=Held::None&&f.held[i]!=Held::Camera)
                drawMesh(f.held[i]==Held::Apple?x.appleMesh:x.pesterBallMesh,heldItemPose(hand,i));
        }
        drawMesh(x.cameraMesh,camera,leftCamera,press);
        if(!g.message.empty()) {
            // One binocular panel, after world rendering and outside the lens
            // pass. Tutorials remain readable while the camera points away.
            Pose panel=compose(meters(f.head),Pose{{},{0,-.18f,-.85f}});
            auto lines=messageLines(g.message);float height=.055f*float(lines.size())+(g.messageContinue?.11f:.06f);
            box(v,panel,{0,-height/2+.035f,-.012f},{.46f,height/2,.008f},{.025f,.04f,.07f});
            for(size_t line=0;line<lines.size();line++)label(v,panel,lines[line].c_str(),-.43f,-float(line)*.055f,.0034f,{1,1,1});
            if(g.messageContinue)label(v,panel,"PRESS A / X TO CONTINUE",-.27f,-height+.075f,.0034f,{1,.8f,.25f});
        }
        Pose display=compose(camera,Pose{{},{leftCamera?.008f:-.008f,-.004f,.050f}});screenQuad(v,display,.116f,.087f);
        // Focus ring is geometry on the screen; it cannot contaminate scoring.
        Color ring=focus?Color{1,.1f,.08f}:Color{.92f,.92f,.92f};
        for(int i=0;i<32;i++) {
            float a=2*pi*i/32,b=2*pi*(i+1)/32;
            Vec3 p{std::cos(a)*.006f,std::sin(a)*.006f,.001f},q{std::cos(b)*.006f,std::sin(b)*.006f,.001f};
            triangle(v,display,p,q,{q.x*1.15f,q.y*1.15f,q.z},ring);
            triangle(v,display,p,{q.x*1.15f,q.y*1.15f,q.z},{p.x*1.15f,p.y*1.15f,p.z},ring);
        }
        // Two seven-segment film-count digits below the viewfinder.
        constexpr unsigned digits[]={0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
        for(int digit=0;digit<2;digit++) {
            unsigned bits=digits[(digit?g.film:g.film/10)%10];float ox=(leftCamera?-.041f:.032f)+digit*.009f,oy=-.038f;
            const Vec3 centers[]={{0,.012f,0},{.004f,.008f,0},{.004f,.002f,0},{0,-.002f,0},{-.004f,.002f,0},{-.004f,.008f,0},{0,.005f,0}};
            for(int segment=0;segment<7;segment++)if(bits&(1<<segment)) {
                bool horizontal=segment==0||segment==3||segment==6;
                box(v,camera,{ox+centers[segment].x,oy+centers[segment].y,.052f},horizontal?Vec3{.003f,.0007f,.0003f}:Vec3{.0007f,.0025f,.0003f},{.7f,1,.7f});
            }
        }
    } else {
        Pose panel{{},{0,interaction.settings.eyeHeight,-1.6f}};
        if(optionsOpen) {
            box(v,panel,{0,0,-.03f},{.8f,.6f,.02f},{.03f,.07f,.12f});
            label(v,panel,"VR OPTIONS",-.62f,.48f,.015f,{1,.8f,.3f});
            char rows[6][64];const auto& settings=interaction.settings;
            std::snprintf(rows[0],64,"HAND  %s",settings.leftHanded?"LEFT":"RIGHT");
            std::snprintf(rows[1],64,"EYE HEIGHT  %.2f M",settings.eyeHeight);
            std::snprintf(rows[2],64,"RENDER SCALE  %.1f",settings.renderScale);
            std::snprintf(rows[3],64,"THROW STRENGTH  %.1f",settings.throwStrength);
            std::snprintf(rows[4],64,"RECENTER");std::snprintf(rows[5],64,"DONE");
            for(int row=0;row<6;row++)label(v,panel,rows[row],-.62f,.27f-row*.11f,.010f,row==optionsRow?Color{1,.8f,.2f}:Color{.9f,.95f,1});
            label(v,panel,"TRIGGER OR A/X SELECTS - STICK MOVES",-.67f,-.47f,.0065f,{.65f,.75f,.85f});
            label(v,panel,"RENDER SCALE APPLIES ON NEXT LAUNCH",-.67f,-.54f,.0065f,{.65f,.75f,.85f});
        }else screenQuad(v,panel,1.6f,1.2f);
        for(int i=0;i<2;i++)if(t.hands[i].tracked) {
            Pose hand=interaction.localPose(t.hands[i].grip);x.hands[i].draw(v,handMeshPose(hand),t.hands[i],Held::None);
            if(i!=(interaction.settings.leftHanded?0:1))continue;
            Pose aim=interaction.localPose(t.hands[i].aim);Vec3 dir=rotate(aim.orientation,{0,0,-1});
            if(dir.z<-.001f){float distance=(-1.6f-aim.position.z)/dir.z;if(distance>0){Vec3 hit=aim.position+dir*distance;
                if(std::abs(hit.x)<.8f&&std::abs(hit.y-interaction.settings.eyeHeight)<.6f){Pose cursor{{},hit};box(v,cursor,{0,0,.002f},{.004f,.004f,.002f},{.15f,1,1});}}}
        }
    }
    x.opaqueIndices.clear();x.transparentTriangles.clear();
    for(unsigned first=0;first<v.size();first+=3) {
        if(v[first].alpha<1||v[first+1].alpha<1||v[first+2].alpha<1)
            x.transparentTriangles.push_back({first,(v[first].position+v[first+1].position+v[first+2].position)*(1.f/3)});
        else for(unsigned k=0;k<3;k++)x.opaqueIndices.push_back(first+k);
    }
    } // Both eyes share exactly the same accessory geometry snapshot.
#ifdef __ANDROID__
    if(v.empty()&&x.rigidDraws.empty())return;
#else
    if(v.empty())return;
#endif
    Pose ep=g.course?meters(eye):interaction.localPose(eye);
    const auto transparent=transparentIndices(x.transparentTriangles,ep);
#ifdef __ANDROID__
    x.draw(color,depth,screen,v,transparent,ep,fov);
#else
    size_t size=v.size()*sizeof(Vertex);if(size>x.capacity) {
        x.vertices.Reset();x.capacity=size*2;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=x.capacity;desc.Height=1;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ok(x.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&x.vertices)));
    }
    if(rebuild){void* data;D3D12_RANGE range{0,0};ok(x.vertices->Map(0,&range,&data));std::memcpy(data,v.data(),size);x.vertices->Unmap(0,nullptr);}
    const size_t indexSize=(x.opaqueIndices.size()+transparent.size())*sizeof(unsigned);
    if(indexSize>x.indexCapacity) {
        x.indices.Reset();x.indexCapacity=indexSize*2;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=x.indexCapacity;desc.Height=1;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ok(x.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&x.indices)));
    }
    if(rebuild||!transparent.empty()) {
        void* data;D3D12_RANGE range{0,0};ok(x.indices->Map(0,&range,&data));auto* dest=static_cast<unsigned*>(data);
        std::copy(x.opaqueIndices.begin(),x.opaqueIndices.end(),dest);std::copy(transparent.begin(),transparent.end(),dest+x.opaqueIndices.size());x.indices->Unmap(0,nullptr);
    }
    x.device->CreateRenderTargetView(color,nullptr,x.rtv->GetCPUDescriptorHandleForHeapStart());x.device->CreateDepthStencilView(depth,nullptr,x.dsv->GetCPUDescriptorHandleForHeapStart());
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=screen->GetDesc().Format;sd.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;sd.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sd.Texture2D.MipLevels=1;x.device->CreateShaderResourceView(screen,&sd,x.srv->GetCPUDescriptorHandleForHeapStart());
    x.begin();auto rt=x.rtv->GetCPUDescriptorHandleForHeapStart(),ds=x.dsv->GetCPUDescriptorHandleForHeapStart();x.list->OMSetRenderTargets(1,&rt,FALSE,&ds);
    auto desc=color->GetDesc();D3D12_VIEWPORT viewport{0,0,float(desc.Width),float(desc.Height),0,1};D3D12_RECT scissor{0,0,LONG(desc.Width),LONG(desc.Height)};x.list->RSSetViewports(1,&viewport);x.list->RSSetScissorRects(1,&scissor);
    x.list->SetPipelineState(x.pipeline.Get());x.list->SetGraphicsRootSignature(x.root.Get());ID3D12DescriptorHeap* heaps[]={x.srv.Get()};x.list->SetDescriptorHeaps(1,heaps);x.list->SetGraphicsRootDescriptorTable(1,x.srv->GetGPUDescriptorHandleForHeapStart());
    auto vp=multiply(view(ep),projection(fov,.02f,1000));x.list->SetGraphicsRoot32BitConstants(0,16,vp.data(),0);
    D3D12_VERTEX_BUFFER_VIEW vb{x.vertices->GetGPUVirtualAddress(),UINT(size),sizeof(Vertex)};x.list->IASetVertexBuffers(0,1,&vb);x.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D12_INDEX_BUFFER_VIEW ib{x.indices->GetGPUVirtualAddress(),UINT(indexSize),DXGI_FORMAT_R32_UINT};x.list->IASetIndexBuffer(&ib);
    x.list->DrawIndexedInstanced(UINT(x.opaqueIndices.size()),1,0,0,0);
    if(!transparent.empty()) {
        x.list->SetPipelineState(x.transparentPipeline.Get());
        x.list->DrawIndexedInstanced(UINT(transparent.size()),1,UINT(x.opaqueIndices.size()),0,0);
    }
    x.finish();
#endif
}
}
