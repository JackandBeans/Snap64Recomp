#pragma once
#include "vr_service.h"
#include "vr_vertex_motion.h"
#include "hle/rt64_workload_queue.h"
#ifdef SNAP_QUEST_BENCHMARK
#include <json/json.hpp>
#include <fstream>
#endif

namespace snap::vr {
// Producer publishes CPU descriptions only. The XR consumer owns these copies
// and its GPU resources; no mutable producer Workload or upload memory escapes.
struct RenderSnapshot {
    RT64::WorkloadQueue data;
    RT64::GameFrame current,previous;
    GameState currentGame,previousGame;
    RT64::TextureCache* textures;
    double published=0,sourceCpu=0,sourceGpu=0,sourcePeriod=1.0/30;
    unsigned vertexCandidates=0,vertexMatched=0,movingVertices=0,vertexChanged=0;
    float maxVertexDelta=0;
    void prepareVertexMotion(const RT64::WorkloadQueue& producer) {
        if(!previous.matched||previousGame.sourceSeconds>=currentGame.sourceSeconds)return;
        for(auto wi:current.workloads) {
            const auto& map=current.frameMap.workloads[wi];
            if(!map.mapped)continue;
            auto& d=data.workloads[wi].drawData;
            const auto& p=data.workloads[map.prevWorkloadIndex].drawData;
            const auto topology=vertexTopology(d.worldIndices,d.worldTransformVertexIndices,d.faceIndices);
            const auto previousTopology=vertexTopology(p.worldIndices,p.worldTransformVertexIndices,p.faceIndices);
            for(size_t i=0;i<map.transforms.size();++i) {
                const auto& match=map.transforms[i];
                if(!match.mapped||i>=topology.size()||match.prevTransformIndex>=previousTopology.size())continue;
                const auto& group=d.transformGroups[d.worldTransformGroups[i]];
                const auto pi=match.prevTransformIndex;
                const auto& previousGroup=p.transformGroups[p.worldTransformGroups[pi]];
                if(!group.coherenceId||group.coherenceId!=previousGroup.coherenceId||group.matrixId!=previousGroup.matrixId)continue;
                const auto begin=d.worldTransformVertexIndices[i],count=d.worldTransformVertexCount(uint32_t(i));
                if(!count)continue;
                auto velocities=std::span(d.velFloats).subspan(begin*3,count*3);
                std::fill(velocities.begin(),velocities.end(),0.f);
                ++vertexCandidates;
                const auto prevBegin=p.worldTransformVertexIndices[pi],prevCount=p.worldTransformVertexCount(pi);
                const VertexMesh cur{std::span(d.posFloats).subspan(begin*3,count*3),std::span(d.tcFloats).subspan(begin*2,count*2),topology[i]};
                const VertexMesh prev{std::span(p.posFloats).subspan(prevBegin*3,prevCount*3),std::span(p.tcFloats).subspan(prevBegin*2,prevCount*2),previousTopology[pi]};
                const bool changed=count!=prevCount||!std::equal(cur.positions.begin(),cur.positions.end(),prev.positions.begin());
                vertexChanged+=changed;
                const bool cut=match.rigidBody.autoRejectedTranslation||match.rigidBody.snapDiscontinuityLatch||
                    producer.workloads[wi].snapHasSteppedId(group.coherenceId);
#ifdef SNAP_QUEST_BENCHMARK
                static unsigned dumped=0;
                if(changed&&dumped<16&&std::getenv("SNAP_QUEST_VERTEX_AUDIT")) {
                    ++dumped;
                    auto mesh=[](const VertexMesh& m){return nlohmann::json{
                        {"positions",std::vector<float>(m.positions.begin(),m.positions.end())},
                        {"uv",std::vector<float>(m.texcoords.begin(),m.texcoords.end())},
                        {"topology",m.topology.corners},{"valid",m.topology.valid}};};
                    std::ofstream("benchmark/results/vertex-pair-"+std::to_string(dumped)+".json")<<nlohmann::json{
                        {"object",group.coherenceId},{"matrix",group.matrixId},{"cut",cut},{"current",mesh(cur)},{"previous",mesh(prev)}}.dump();
                }
#endif
                if(cut)continue;
                const auto motion=vertexMotion(cur,prev,velocities);
                vertexMatched+=motion.matched;movingVertices+=motion.moving;
                maxVertexDelta=std::max(maxVertexDelta,motion.maxDelta);
            }
        }
    }
    static void copyDescription(RT64::Workload& dst,const RT64::Workload& src) {
        dst.drawData=src.drawData;dst.fbPairs=src.fbPairs;
        dst.fbPairCount=src.fbPairCount;dst.submissionFrame=src.submissionFrame;
        dst.workloadId=src.workloadId;dst.extended=src.extended;
        dst.snapVRFrame=src.snapVRFrame;dst.snapVREpoch=src.snapVREpoch;
        dst.debuggerCamera=src.debuggerCamera;dst.debuggerRenderer=src.debuggerRenderer;
    }
    RenderSnapshot(RT64::WorkloadQueue& producer,const RT64::GameFrame& cur,const RT64::GameFrame& prev)
        :current(cur),previous(prev),textures(producer.ext.textureCache) {
        for(auto i:prev.workloads)copyDescription(data.workloads[i],producer.workloads[i]);
        for(auto i:cur.workloads)copyDescription(data.workloads[i],producer.workloads[i]);
        auto& state=shared();
        {std::lock_guard lock(state.mutex);
            auto lookup=[&](const RT64::GameFrame& frame,GameState& game) {
                if(frame.workloads.empty())return;
                const auto& w=producer.workloads[frame.workloads.back()];
                for(auto it=state.gameHistory.rbegin();it!=state.gameHistory.rend();++it)
                    if(it->epoch==w.snapVREpoch&&it->frame==w.snapVRFrame){game=*it;break;}
            };
            lookup(cur,currentGame);lookup(prev,previousGame);
        }
        previous.matched=previous.matched&&previousGame.sourceSeconds>0&&previousGame.epoch==currentGame.epoch;
        // The source renderer keeps its original scoring vertices. These
        // immutable display-only velocities are uploaded once per source pair
        // and sampled by RSPProcessCS at each predicted display-time alpha.
        prepareVertexMotion(producer);
        if(!cur.workloads.empty()) {
            const auto hz=producer.workloads[cur.workloads.back()].viOriginalRate;
            if(hz>0)sourcePeriod=1.0/hz;
        }
        published=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        sourceCpu=producer.snapSourceCpuMs;sourceGpu=producer.snapSourceGpuMs;
        textures->incrementLock();
    }
    ~RenderSnapshot(){textures->decrementLock();}
};
}
