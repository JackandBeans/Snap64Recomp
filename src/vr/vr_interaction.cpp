#include "vr_interaction.h"
#include "vr_menu.h"

namespace snap::vr {
void Interaction::recenter(const Tracking& t) {
    if (!t.headValid) return;
    origin={yaw(heading(t.head.orientation)),{t.head.position.x,t.head.position.y-settings.eyeHeight,t.head.position.z}};
    centered=true;
    for (auto& h:history) h.clear();
}
Pose Interaction::localPose(Pose p) const { return compose(inverse(origin),p); }
Pose Interaction::cartPose(const GameState& g) const {
    // Snap's route forward is +Z. Rotate the OpenXR -Z forward into it.
    return {yaw(g.cartYaw+pi),g.cartPosition};
}
Pose Interaction::toWorld(Pose p,const GameState& g) const {
    p=localPose(p); p.position=p.position*settings.unitsPerMeter;
    return compose(cartPose(g),p);
}
InteractionFrame Interaction::update(const Tracking& t,const GameState& g) {
    InteractionFrame out; out.frame=t.frame; out.epoch=g.epoch;
    if (!centered && t.headValid) recenter(t);
    bool reset=(epoch!=g.epoch)||!g.course||!t.focused||!t.headValid||g.paused||g.cinematic;
    if (reset) {
        held={}; armed={}; gripping={}; triggering={};
        for (auto& h:history) h.clear();
    }
    if (wasFocused&&!t.focused&&g.course&&!g.paused) out.pause=true;
    wasFocused=t.focused; epoch=g.epoch;
    float dt=float(std::clamp(t.seconds-lastTime,0.0,0.05)); lastTime=t.seconds;
    out.head=toWorld(t.head,g);
    Pose dock{{},cameraDock}; dock.position=dock.position*settings.unitsPerMeter;
    out.lens=compose(compose(cartPose(g),dock),Pose{{},{0,0,-.14f*settings.unitsPerMeter}});
    for (int i=0;i<2;i++) {
        const auto& h=t.hands[i]; auto& samples=history[i];
        bool grip=h.squeeze>(gripping[i]?0.35f:0.65f),trigger=h.trigger>0.6f;
        out.hands[i]=toWorld(h.grip,g);
        if (!h.tracked||reset) {
            held[i]=Held::None; samples.clear(); armed[i]=false;
        } else {
            if (!grip) armed[i]=true; // regain tracking with grip held cannot grab/throw
            Pose local=localPose(h.grip);
            if (!samples.empty()&&(t.seconds<=samples.back().time||t.seconds-samples.back().time>.12)) samples.clear();
            const Vec3 itemPosition=heldItemPose(local).position;
            samples.push_back({t.seconds,itemPosition});
            while(samples.size()>2 && t.seconds-samples.front().time>.24) samples.pop_front();
            if (grip&&!gripping[i]&&armed[i]&&held[i]==Held::None) {
                float cameraDistance=length(local.position-cameraDock);
                float itemDistance=std::min(g.apples?length(local.position-appleBin):1.f,g.pesterBalls?length(local.position-pesterBin):1.f);
                if (cameraDistance<0.22f&&cameraDistance<=itemDistance&&held[1-i]!=Held::Camera) held[i]=Held::Camera;
                else if(g.itemReady&&t.seconds>=nextThrow) {
                    float apple=g.apples?length(local.position-appleBin):1.f;
                    float pester=g.pesterBalls?length(local.position-pesterBin):1.f;
                    if(apple<.20f&&apple<=pester)held[i]=Held::Apple;
                    else if(pester<.20f)held[i]=Held::PesterBall;
                }
                if(held[i]==Held::Apple||held[i]==Held::PesterBall){samples.clear();samples.push_back({t.seconds,itemPosition});}
            }
            if (!grip&&gripping[i]) {
                if ((held[i]==Held::Apple||held[i]==Held::PesterBall)&&samples.size()>=2) {
                    if(t.seconds-samples.front().time>=.035) {
                        Vec3 velocity=throwVelocity(samples);
                        velocity=rotate(cartPose(g).orientation,velocity*(settings.unitsPerMeter*settings.throwStrength))+g.cartVelocity;
                        out.throws.push_back({held[i],heldItemPose(out.hands[i],settings.unitsPerMeter).position,velocity,g.epoch});
                        nextThrow=t.seconds+0.25;
                    }
                }
                held[i]=Held::None;
            }
            if(held[i]==Held::Camera) {
                out.cameraHeld=true;
                // Pitch the camera forward around its palm attachment. Apply
                // the same correction to the optical pose and the visible body.
                Pose aim=toWorld(h.aim,g);Pose wrist=handMeshPose(out.hands[i]);
                aim.orientation=compose(aim,Pose{{-.70710678f,0,0,.70710678f},{}}).orientation;
                Vec3 palm=wrist.position+rotate(wrist.orientation,Vec3{i==0?.025f:-.025f,.015f,-.055f}*settings.unitsPerMeter);
                Vec3 socket{i==0?-.092f:.092f,-.012f,.010f};
                Pose body{aim.orientation,palm-rotate(aim.orientation,socket*settings.unitsPerMeter)};
                out.lens=compose(body,Pose{{},{0,0,-.14f*settings.unitsPerMeter}});
                fovY=std::clamp(fovY-h.stickY*30*dt,20.0f,60.0f);
                out.shutter=trigger&&!triggering[i]&&g.film>0;
            }
            out.dash=out.dash||h.secondary;
            out.flute=out.flute||(h.primary&&!primary[i]);
            out.pause=out.pause||(h.menu&&!menus[i]);
        }
        gripping[i]=grip; triggering[i]=trigger; primary[i]=h.primary; menus[i]=h.menu;
    }
    if(out.cameraHeld&&hadCamera) {
        int other=held[0]==Held::Camera?1:0;
        if(held[other]==Held::None&&t.hands[other].tracked&&t.hands[other].squeeze>.65f&&length(out.hands[other].position-out.lens.position)<25) {
            Quat a=lastLens,b=out.lens.orientation;float sign=(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w)<0?-1.f:1.f;
            float blend=1-std::exp(-dt*25);Quat q{a.x*(1-blend)+b.x*sign*blend,a.y*(1-blend)+b.y*sign*blend,a.z*(1-blend)+b.z*sign*blend,a.w*(1-blend)+b.w*sign*blend};
            float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
            Vec3 lensToPalm{held[0]==Held::Camera?-.092f:.092f,-.012f,.150f};
            lensToPalm=lensToPalm*settings.unitsPerMeter;
            Vec3 anchor=out.lens.position+rotate(out.lens.orientation,lensToPalm);
            out.lens.orientation={q.x/n,q.y/n,q.z/n,q.w/n};
            out.lens.position=anchor-rotate(out.lens.orientation,lensToPalm);
        }
    }
    hadCamera=out.cameraHeld;lastLens=out.lens.orientation;
    out.lensInCart=compose(inverse(cartPose(g)),out.lens);
    out.held=held; out.fovY=fovY;
    return out;
}
}
