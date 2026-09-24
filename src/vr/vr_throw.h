#pragma once
#include "vr_math.h"
#include <deque>

namespace snap::vr {
// Adapted from DramaticShapeVoxelMod's VR release path, with a recent-time
// regression and gentler gain replacing peak selection after controller testing.
// Positions are the held object's center in calibrated tracking-space meters.
struct ThrowSample { double time; Vec3 position; };
inline Vec3 throwVelocity(const std::deque<ThrowSample>& samples) {
    if(samples.size()<3)return {};
    // Fit the recent release, rather than selecting an older maximum-speed
    // segment. Center time at release to retain precision over long sessions.
    double sumT=0,sumTT=0;Vec3 sumP{},sumTP{};unsigned count=0;
    double oldest=samples.back().time;
    for(const auto& sample:samples) {
        double t=sample.time-samples.back().time;
        if(t<-.080001)continue;
        oldest=std::min(oldest,sample.time);sumT+=t;sumTT+=t*t;
        sumP=sumP+sample.position;sumTP=sumTP+sample.position*float(t);++count;
    }
    double denom=count*sumTT-sumT*sumT;
    if(count<3||samples.back().time-oldest<.03||denom<1e-8)return {};
    Vec3 result=(sumTP*float(count)-sumP*float(sumT))*float(1/denom);
    float speed=length(result);
    if(!std::isfinite(speed)||speed>15)return {}; // discontinuous tracking
    float blend=std::clamp((speed-.3f)/.9f,0.f,1.f);
    blend=blend*blend*(3-2*blend);
    return result*(1+1.3f*blend); // smooth gain up to 2.3x; drops remain gentle
}
inline Pose heldItemPose(Pose hand,float unitsPerMeter=1) {
    return compose(hand,Pose{{},Vec3{0,-.02f,-.06f}*unitsPerMeter});
}
}
