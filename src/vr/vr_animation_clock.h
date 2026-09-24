#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace snap::vr {
// Select a presentation pose from timestamps, independent of source job count.
// The caller supplies CLOCK_MONOTONIC times and a matched pair in one epoch.
class AnimationClock {
    uint64_t epoch_=UINT64_MAX;
    double last_=0;
    double presentationOffset_=0;
public:
    struct Sample {double time=0,sourceAge=0;float alpha=1;bool stale=false;};
    void reset(){epoch_=UINT64_MAX;last_=0;presentationOffset_=0;}
    double presentationOffset()const{return presentationOffset_;}
    double target(uint64_t epoch,double newest,double predicted,double period=1.0/30.0) {
        if(epoch!=epoch_){epoch_=epoch;last_=0;presentationOffset_=newest-predicted;}
        return std::max(last_,predicted+presentationOffset_-period);
    }
    Sample sample(uint64_t epoch,double a,double b,double predicted,double period=1.0/30.0) {
        if(!std::isfinite(a)||!std::isfinite(b)||!std::isfinite(predicted)||b<=a)
            return {last_,0,1,true};
        // Source poses describe simulation time; predicted display time leads
        // wall time by the runtime's presentation pipeline. Establish their
        // playback phase once, when the first complete pair becomes available.
        // Never slide this anchor after a stall: that would accumulate latency.
        const double desired=target(epoch,b,predicted,period);
        // A publication older than the pose already displayed cannot rewind it.
        if(b<last_)return {last_,std::max(0.0,predicted-b),1,true};
        const double selected=std::clamp(std::max(desired,last_),a,b);
        last_=selected;
        return {selected,std::max(0.0,predicted-b),float((selected-a)/(b-a)),desired>b+1e-6};
    }
};
}
