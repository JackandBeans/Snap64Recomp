#pragma once
#include <algorithm>
#include <cstdint>
namespace snap::vr {
// Wall-clock duration, canceled by pause/focus loss or a course transition.
struct FluteTimer {
    uint64_t epoch=0;
    double until=0;
    bool active=false;
    enum Action { None, Play, Stop };
    Action update(double now,uint64_t scene,bool allowed,bool press) {
        if(scene!=epoch){active=false;epoch=scene;}
        if(!allowed){bool stop=active;active=false;return stop?Stop:None;}
        if(press){until=now+10;active=true;return Play;}
        if(active&&now>=until){active=false;return Stop;}
        return None;
    }
    float remaining(double now)const{return active?float(std::max(0.0,until-now)):0.f;}
};
}
