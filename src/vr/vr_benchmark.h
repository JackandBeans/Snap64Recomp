#pragma once
#ifdef SNAP_QUEST_BENCHMARK
#include "vr_interaction.h"
#include <chrono>
#include <cstdio>
#include <filesystem>

namespace snap::vr {
// Only compiled into the isolated benchmark. Never synthesize head/eye poses,
// focus, or tracking validity: the real runtime must supply those.
struct QuestBenchmark {
    FILE* frames=nullptr;
    std::array<char,65536> buffer{};
    double started=0,lastFlush=0,courseStart=0;
    uint64_t courseEpoch=0;
    bool entered=false,complete=false,hadFocus=false,realControllers=false;
    Pose origin{};
    uint64_t samples=0;
    static double clock() {return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
    QuestBenchmark() {
        std::filesystem::create_directories("benchmark/results");
        frames=std::fopen("benchmark/results/frames.csv","w");
        if(frames) {
            std::setvbuf(frames,buffer.data(),_IOFBF,buffer.size());
            std::fputs("time,predicted,tracking_frame,epoch,game_frame,source_frame,workload,alpha,course,cinematic,paused,focused,head_valid,width,height,hz,cpu_ms,gpu_ms,wait_ms,left_ms,right_ms,viewfinder_ms,props_ms,copy_ms,camera_held,film,level_id\n",frames);
        }
        started=clock();
    }
    ~QuestBenchmark(){if(frames)std::fclose(frames);}
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
        unsigned width,unsigned height,float hz,double cpu,double gpu,double wait,const std::array<double,7>& stages,bool camera,uint32_t overlay) {
        if(!frames)return;
        double now=clock();
        if(entered&&!g.course)complete=true;
        std::fprintf(frames,"%.6f,%.6f,%llu,%llu,%llu,%llu,%llu,%.6f,%d,%d,%d,%d,%d,%u,%u,%.1f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%u\n",
            now,t.seconds,(unsigned long long)t.frame,(unsigned long long)g.epoch,(unsigned long long)g.frame,
            (unsigned long long)source,(unsigned long long)workload,alpha,g.course,g.cinematic,g.paused,t.focused,t.headValid,width,height,hz,
            cpu,gpu,wait,stages[0],stages[1],stages[2],stages[3],stages[4],camera,g.film,unsigned(g.levelId));
        ++samples;
        if(now-lastFlush>=1||complete) {
            std::fflush(frames);lastFlush=now;
            auto* status=std::fopen("benchmark/results/status.tmp","w");
            if(status){std::fprintf(status,"{\"elapsed\":%.3f,\"entered\":%s,\"complete\":%s,\"focused\":%s,\"head_valid\":%s,\"samples\":%llu,\"overlay\":%u,\"real_controllers\":%s}",
                now-started,entered?"true":"false",complete?"true":"false",t.focused?"true":"false",t.headValid?"true":"false",(unsigned long long)samples,overlay,realControllers?"true":"false");
                std::fclose(status);std::rename("benchmark/results/status.tmp","benchmark/results/status.json");}
        }
    }
};
inline QuestBenchmark& questBenchmark(){static QuestBenchmark instance;return instance;}
}
#endif
