#include "vr_service.h"
#include <cstdlib>
namespace snap::vr {
std::atomic<bool> requested{false};
bool preview=false;
std::atomic<unsigned> displayRate{90};
SharedState& shared(){static SharedState s;return s;}
bool input(uint16_t* buttons,float* x,float* y) {
    if(!requested.load())return false;
    auto& s=shared();std::lock_guard lock(s.mutex);
    if(preview&&!std::getenv("SNAP_VR_NAME_TEST")&&!std::getenv("SNAP_VR_POINTER_TEST")&&
       !(std::getenv("SNAP_VR_LATENCY_TEST")&&s.game.course&&!s.game.cinematic))return false;
    // Guest controllers are polled faster than the 30 Hz gameplay/UI logic.
    // Keep an action across at least one complete simulation tick.
    const auto now=std::chrono::steady_clock::now();
    if(now>=s.pulseUntil)s.pulseHeld=0;
    if(s.pulses){if((s.pulses&0x8000)&&s.shutterTime!=std::chrono::steady_clock::time_point{})s.shutterPollTime=now;s.pulseHeld|=s.pulses;s.pulseUntil=now+std::chrono::milliseconds(50);s.pulses=0;}
    if(!s.tracking.focused||!s.tracking.headValid)s.pulseHeld=0;
    *buttons=s.buttons|s.pulseHeld;*x=s.stickX;*y=s.stickY;return true;
}
extern "C" void snap_vr_shutdown(){shutdown();}
extern "C" unsigned snap_vr_refresh_rate(){return displayRate.load();}
extern "C" bool snap_vr_enabled(){return snap::vr::requested.load();}
extern "C" bool snap_vr_world_active(){if(!requested.load())return false;auto& s=shared();std::lock_guard lock(s.mutex);return (s.game.course||s.game.cinematic)&&!s.game.paused;}
extern "C" void snap_vr_render(RT64::WorkloadQueue* q,RT64::GameFrame* f,const RT64::GameFrame* previous,float weight,RT64::RenderTarget* presented){snap::vr::render(*q,*f,*previous,weight,presented);}
void sceneChanged(bool course) {
    auto& s=shared();std::lock_guard lock(s.mutex);
    s.menuPointer=s.menuClick={};s.namePointer=s.nameClick={};
    s.gameHistory.clear();s.game.message.clear();s.game.messageContinue=false;s.viewTurned=false;
    s.pulseHeld=0;s.pulseUntil={};s.fluteRequest=false;s.game.fluteSeconds=0;s.game.fluteUnlocked=false;
    s.shutterTime=s.shutterPollTime={};s.shutterSerial=0;
    ++s.game.epoch;s.game.course=course;s.game.cinematic=course;s.game.smoke.clear();s.game.frame=0;s.releases.clear();s.interaction={};s.buttons=s.pulses=0;s.focusVisible=false;s.focusFrame=0;
}
#ifndef SNAP_ENABLE_VR
void render(RT64::WorkloadQueue&,RT64::GameFrame&,const RT64::GameFrame&,float,RT64::RenderTarget*){}
void shutdown(){}
#endif
}
