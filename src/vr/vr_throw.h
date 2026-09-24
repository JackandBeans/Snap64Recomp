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
// DramaticShape HandProp.OFFSET_L/R and data/ball_grip.lua. Offsets are
// in OpenXR grip space, meters; the wrist nudge and ball seat must agree.
inline Pose itemHandPose(Pose hand,unsigned side,float unitsPerMeter=1) {
    constexpr float h=.7071067811865475f;
    return compose(hand,Pose{{-h,0,0,h},Vec3{side==0?-.015f:.015f,.06f,0}*unitsPerMeter});
}
inline Pose heldItemPose(Pose hand,unsigned side,float unitsPerMeter=1) {
    constexpr float h=.7071067811865475f;
    return compose(hand,Pose{{side==0?h:-h,0,h,0},Vec3{side==0?.039f:-.039f,-.015f,-.001f}*unitsPerMeter});
}
}
