#pragma once
#include "vr_service.h"
#include "hle/rt64_workload_queue.h"

namespace snap::vr {
// Producer publishes CPU descriptions only. The XR consumer owns these copies
// and its GPU resources; no mutable producer Workload or upload memory escapes.
struct RenderSnapshot {
    RT64::WorkloadQueue data;
    RT64::GameFrame current,previous;
    GameState currentGame,previousGame;
    RT64::TextureCache* textures;
    double published=0,sourceCpu=0,sourceGpu=0,sourcePeriod=1.0/30;
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
