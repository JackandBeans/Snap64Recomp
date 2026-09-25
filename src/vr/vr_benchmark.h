#pragma once
#ifdef SNAP_QUEST_BENCHMARK
#include "vr_interaction.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <cstring>

namespace snap::vr {
// Only compiled into the isolated benchmark. Never synthesize head/eye poses,
// focus, or tracking validity: the real runtime must supply those.
struct QuestBenchmark {
    FILE* frames=nullptr;
    FILE* gpuFrames=nullptr;
    FILE* animationFrames=nullptr;
    FILE* poseFrames=nullptr;
    FILE* vertexGpuFrames=nullptr;
    std::array<char,65536> poseBuffer{};
    std::array<char,65536> buffer{};
    double started=0,lastFlush=0,courseStart=0;
    uint64_t courseEpoch=0;
    bool entered=false,complete=false,hadFocus=false,realControllers=false;
    bool introSeen=false,introComplete=false;
    Pose origin{};
    uint64_t samples=0;
    static double clock() {return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
    QuestBenchmark() {
        std::filesystem::create_directories("benchmark/results");
        frames=std::fopen("benchmark/results/frames.csv","w");
        gpuFrames=std::fopen("benchmark/results/gpu.csv","w");
        animationFrames=std::fopen("benchmark/results/animation.csv","w");
        poseFrames=std::fopen("benchmark/results/pose_uploads.csv","w");
        if(std::getenv("SNAP_QUEST_VERTEX_AUDIT")) {
            vertexGpuFrames=std::fopen("benchmark/results/vertex_gpu.csv","w");
            if(vertexGpuFrames)std::fputs("tracking_frame,eye,object,matrix,vertex,alpha,deforming,max_error,from_source,passed\n",vertexGpuFrames);
        }
        if(poseFrames){std::setvbuf(poseFrames,poseBuffer.data(),_IOFBF,poseBuffer.size());
            std::fputs("tracking_frame,eye,object,matrix,alpha,source_motion,from_a,from_b,pose_hash,blends\n",poseFrames);}
        if(animationFrames)std::fputs("tracking_frame,source_a,source_b,published,animation_time,source_age,alpha,stale,matched,mutable_images,vertex_candidates,vertex_matched,moving_vertices,max_vertex_delta,vertex_changed\n",animationFrames);
        if(gpuFrames)std::fputs("tracking_frame,gpu_ms,left_gpu_ms,right_gpu_ms,viewfinder_gpu_ms\n",gpuFrames);
        if(frames) {
            std::setvbuf(frames,buffer.data(),_IOFBF,buffer.size());
            std::fputs("time,predicted,tracking_frame,epoch,game_frame,source_frame,workload,alpha,course,cinematic,paused,focused,head_valid,width,height,hz,cpu_ms,gpu_ms,wait_ms,left_ms,right_ms,viewfinder_ms,props_ms,copy_ms,camera_held,film,level_id,left_gpu_ms,right_gpu_ms,viewfinder_gpu_ms,xr_wait_ms,xr_begin_ms,xr_end_ms,source_cpu_ms,source_gpu_ms\n",frames);
        }
        started=clock();
    }
    ~QuestBenchmark(){if(frames)std::fclose(frames);if(gpuFrames)std::fclose(gpuFrames);if(animationFrames)std::fclose(animationFrames);if(poseFrames)std::fclose(poseFrames);if(vertexGpuFrames)std::fclose(vertexGpuFrames);}
    void finish() {
        for(auto file:{&frames,&gpuFrames,&animationFrames,&poseFrames,&vertexGpuFrames}) {
            if(*file){std::fclose(*file);*file=nullptr;}
        }
        if(auto* done=std::fopen("benchmark/results/finished.json","w")) {
            std::fputs("{\"finished\":true}",done);std::fclose(done);
        }
        std::filesystem::remove("benchmark/finish.request");
    }
    void poseUpload(uint64_t frame,unsigned eye,uint32_t object,uint32_t matrix,float alpha,
        const void* a,const void* b,const void* pose,bool blends) {
        if(!poseFrames)return;
        float av[16],bv[16],pv[16];std::memcpy(av,a,sizeof(av));std::memcpy(bv,b,sizeof(bv));std::memcpy(pv,pose,sizeof(pv));
        float motion=0,fromA=0,fromB=0;
        for(unsigned i=0;i<16;++i){motion=std::max(motion,std::abs(av[i]-bv[i]));fromA=std::max(fromA,std::abs(pv[i]-av[i]));fromB=std::max(fromB,std::abs(pv[i]-bv[i]));}
        if(motion<1e-4f)return;
        uint64_t hash=14695981039346656037ull;
        const auto* bytes=static_cast<const unsigned char*>(pose);
        for(unsigned i=0;i<sizeof(pv);++i){hash^=bytes[i];hash*=1099511628211ull;}
        std::fprintf(poseFrames,"%llu,%u,%u,%u,%.6f,%.6f,%.6f,%.6f,%llu,%d\n",
            (unsigned long long)frame,eye,object,matrix,alpha,motion,fromA,fromB,(unsigned long long)hash,blends);
    }
    void animationSample(uint64_t frame,double a,double b,double published,double time,double age,float alpha,bool stale,bool matched,bool mutableImages,
        unsigned vertexCandidates,unsigned vertexMatched,unsigned movingVertices,float maxVertexDelta,unsigned vertexChanged) {
        if(animationFrames)std::fprintf(animationFrames,"%llu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%d,%d,%u,%u,%u,%.6f,%u\n",
            (unsigned long long)frame,a,b,published,time,age,alpha,stale,matched,mutableImages,vertexCandidates,vertexMatched,movingVertices,maxVertexDelta,vertexChanged);
    }
    void gpuSample(uint64_t frame,double span,const std::array<double,3>& views) {
        if(gpuFrames)std::fprintf(gpuFrames,"%llu,%.3f,%.3f,%.3f,%.3f\n",(unsigned long long)frame,span,views[0],views[1],views[2]);
    }
    void script(Tracking& t,const GameState& g) {
        realControllers=t.hands[0].tracked||t.hands[1].tracked;
        if(!t.focused||!t.headValid)return;
        if(!hadFocus){hadFocus=true;origin={yaw(heading(t.head.orientation)),{t.head.position.x,t.head.position.y-1.2f,t.head.position.z}};}
        if(g.course&&g.levelId==0&&!g.cinematic&&!entered){entered=true;courseEpoch=g.epoch;courseStart=t.seconds;}
        if(!g.course)return;
        const double elapsed=entered?t.seconds-courseStart:0;
        const double phase=std::fmod(std::max(0.0,elapsed),24.0);
        for(unsigned i=0;i<2;++i) {
            auto& h=t.hands[i];h={};h.tracked=true;
            Pose local{{.70710678f,0,0,.70710678f},{i?.28f:-.28f,.92f,-.38f}};
            if(i==1&&phase>=5&&phase<18) {
                h.squeeze=phase>=5.5?1.f:0.f;
                if(phase>=6) {
                    local.position={.18f,1.12f,-.32f};
                    local.orientation=compose(Pose{yaw(.5f*std::sin(float(phase-6)*.5f)),{}},local).orientation;
                }
                h.trigger=(phase>=10&&phase<10.15)?1.f:0.f;
            }
            h.grip=h.aim=compose(origin,local);
            h.primary=g.messageContinue&&std::fmod(elapsed,2.0)<.15;
        }
    }
    void sample(const Tracking& t,const GameState& g,uint64_t source,uint64_t workload,float alpha,
        unsigned width,unsigned height,float hz,double cpu,double gpu,double wait,const std::array<double,7>& stages,bool camera,uint32_t overlay,
        const std::array<double,3>& viewGpu,const std::array<double,3>& xrTimes,double sourceCpu,double sourceGpu) {
        if(!frames)return;
        double now=clock();
        if(overlay==0xA08E30&&g.cinematic)introSeen=true;
        if(introSeen&&!g.cinematic)introComplete=true;
        if(entered&&!g.course)complete=true;
        std::fprintf(frames,"%.6f,%.6f,%llu,%llu,%llu,%llu,%llu,%.6f,%d,%d,%d,%d,%d,%u,%u,%.1f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
            now,t.seconds,(unsigned long long)t.frame,(unsigned long long)g.epoch,(unsigned long long)g.frame,
            (unsigned long long)source,(unsigned long long)workload,alpha,g.course,g.cinematic,g.paused,t.focused,t.headValid,width,height,hz,
            cpu,gpu,wait,stages[0],stages[1],stages[2],stages[3],stages[4],camera,g.film,unsigned(g.levelId),
            viewGpu[0],viewGpu[1],viewGpu[2],xrTimes[0],xrTimes[1],xrTimes[2],sourceCpu,sourceGpu);
        ++samples;
        if(now-lastFlush>=1||complete) {
            std::fflush(frames);lastFlush=now;
            if(gpuFrames)std::fflush(gpuFrames);
            if(animationFrames)std::fflush(animationFrames);
            if(poseFrames)std::fflush(poseFrames);
            if(vertexGpuFrames)std::fflush(vertexGpuFrames);
            auto* status=std::fopen("benchmark/results/status.tmp","w");
            if(status){std::fprintf(status,"{\"elapsed\":%.3f,\"entered\":%s,\"complete\":%s,\"focused\":%s,\"head_valid\":%s,\"samples\":%llu,\"overlay\":%u,\"real_controllers\":%s,\"intro_seen\":%s,\"intro_complete\":%s}",
                now-started,entered?"true":"false",complete?"true":"false",t.focused?"true":"false",t.headValid?"true":"false",(unsigned long long)samples,overlay,realControllers?"true":"false",introSeen?"true":"false",introComplete?"true":"false");
                std::fclose(status);std::rename("benchmark/results/status.tmp","benchmark/results/status.json");}
        }
    }
};
inline QuestBenchmark& questBenchmark(){static QuestBenchmark instance;return instance;}
}
#endif
