#pragma once
#include <algorithm>
namespace snap::vr {
// Freeze the outgoing eye images until black, then reveal the new view.
// Outgoing frames must never contain the incoming Todd/cart geometry.
struct ViewTransition {
    int mode=-1,target=-1;double started=0;bool fadingOut=false;
    float lastBrightness=1;
    struct Step {bool hold=false;float gain=1;};
    Step update(int desired,double now) {
        if(mode<0){mode=target=desired;started=now-.4;return {};}
        if(desired!=target){target=desired;started=now;fadingOut=true;lastBrightness=1;}
        if(fadingOut) {
            float brightness=1-float(std::clamp((now-started)/.25,0.,1.));
            if(brightness>0){float ratio=brightness/lastBrightness;lastBrightness=brightness;return {true,ratio};}
            mode=target;fadingOut=false;started=now;lastBrightness=0;
            return {false,0};
        }
        return {false,float(std::clamp((now-started)/.4,0.,1.))};
    }
};
}
