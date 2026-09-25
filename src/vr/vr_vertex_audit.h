#pragma once
#ifdef SNAP_QUEST_BENCHMARK
#include "hle/rt64_workload.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace snap::vr {
// Read the real RSP compute output after this frame slot's fence. This is an
// explicitly untimed diagnostic; it never adds a per-pass completion wait.
struct VertexAudit {
    struct Sample {
        uint32_t vertex,object,matrix;
        std::array<float,4> expected,source;
        bool deforming;
    };
    std::unique_ptr<plume::RenderBuffer> readback;
    std::vector<Sample> samples;
    uint64_t frame=0;
    unsigned eye=0;
    float alpha=1;
    static std::array<float,4> project(const hlslpp::float3& position,const hlslpp::float4x4& world,
        const hlslpp::float4x4& vp,const interop::RSPViewport& viewport) {
        auto clip=hlslpp::mul(hlslpp::float4(position,1),hlslpp::mul(world,vp));
        float w=float(clip.w);if(w==0)w=1e-6f;
        auto ndc=clip.xyz/hlslpp::float3(w,-w,w);
        auto screen=ndc*viewport.scale+viewport.translate;
        return {float(screen.x),float(screen.y),float(screen.z),w};
    }
    void record(RT64::RenderWorker* worker,const RT64::Workload& work,uint64_t trackingFrame,unsigned target,float weight) {
        samples.clear();
        if(target>1||!std::getenv("SNAP_QUEST_VERTEX_AUDIT"))return;
        const auto& d=work.drawData;
        const auto& transforms=d.lerpWorldTransforms.size()==d.worldTransforms.size()?d.lerpWorldTransforms:d.worldTransforms;
        std::vector<bool> visited(d.worldTransforms.size()),modified(d.vertexCount());
        for(size_t i=0;i<d.modifyPosUints.size();i+=2)modified.at(d.modifyPosUints[i]>>1)=true;
        for(auto vertex:d.faceIndices) {
            auto transform=d.worldIndices[vertex],projection=d.viewProjIndices[vertex];
            const auto& group=d.transformGroups[d.worldTransformGroups[transform]];
            if(!group.coherenceId||visited[transform]||modified[vertex])continue;
            hlslpp::float3 pos(d.posFloats[vertex*3],d.posFloats[vertex*3+1],d.posFloats[vertex*3+2]);
            hlslpp::float3 velocity(d.velFloats[vertex*3],d.velFloats[vertex*3+1],d.velFloats[vertex*3+2]);
            const auto& vp=d.modViewProjTransforms[projection];const auto& viewport=d.modRspViewports[projection];
            auto expected=project(pos-velocity*(1-weight),transforms[transform],vp,viewport);
            auto source=project(pos,d.worldTransforms[transform],vp,viewport);
            if(expected[3]<=2||expected[0]<0||expected[0]>320||expected[1]<0||expected[1]>240)continue;
            float motion=0;for(unsigned j=0;j<4;++j)motion=std::max(motion,std::abs(expected[j]-source[j]));
            if(motion<.02f)continue;
            samples.push_back({vertex,group.coherenceId,group.matrixId,expected,source,float(hlslpp::length(velocity))>1e-4f});
            visited[transform]=true;
            if(samples.size()==16)break;
        }
        if(samples.empty())return;
        frame=trackingFrame;eye=target;alpha=weight;
        if(!readback)readback=worker->device->createBuffer(plume::RenderBufferDesc::ReadbackBuffer(16*sizeof(float)*4));
        auto* output=work.outputBuffers.screenPosBuffer.buffer.get();
        plume::RenderBufferBarrier barriers[]={{output,plume::RenderBufferAccess::READ},{readback.get(),plume::RenderBufferAccess::WRITE}};
        worker->commandList->barriers(plume::RenderBarrierStage::COPY,barriers,2);
        for(size_t i=0;i<samples.size();++i)
            worker->commandList->copyBufferRegion(readback->at(i*sizeof(float)*4),output->at(samples[i].vertex*sizeof(float)*4),sizeof(float)*4);
        worker->commandList->barriers(plume::RenderBarrierStage::GRAPHICS,plume::RenderBufferBarrier(output,plume::RenderBufferAccess::READ));
    }
    void collect(FILE* output) {
        if(samples.empty())return;
        const auto* gpu=static_cast<const float*>(readback->map());
        for(size_t i=0;i<samples.size();++i) {
            const auto& s=samples[i];float error=0,fromSource=0;bool passed=true;
            for(unsigned j=0;j<4;++j) {
                float delta=std::abs(gpu[i*4+j]-s.expected[j]);
                passed&=std::isfinite(gpu[i*4+j])&&delta<=.02f+std::abs(s.expected[j])*2e-5f;
                error=std::max(error,delta);fromSource=std::max(fromSource,std::abs(gpu[i*4+j]-s.source[j]));
            }
            if(output)std::fprintf(output,"%llu,%u,%u,%u,%u,%.6f,%d,%.6f,%.6f,%d\n",
                (unsigned long long)frame,eye,s.object,s.matrix,s.vertex,alpha,s.deforming,error,fromSource,passed);
        }
        readback->unmap();samples.clear();
    }
};
}
#endif
