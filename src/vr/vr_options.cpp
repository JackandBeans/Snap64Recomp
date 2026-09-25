#include "vr_options.h"
#include "settings.h"
namespace snap::vr {
bool Options::update(const Tracking& t,Interaction& interaction) {
    recenterRequested=false;
    if(!t.focused||!t.headValid){click=trigger=cancel=false;return false;}
    const auto& left=t.hands[0];
    if(left.stickClick&&t.hands[1].stickClick)bothLatch=true;
    if(!left.stickClick&&!t.hands[1].stickClick)bothLatch=false;
    bool clicked=left.tracked&&left.stickClick&&!t.hands[1].stickClick&&!bothLatch;
    bool opened=false;
    if(clicked&&!click){open=!open;opened=open;}
    click=clicked;
    unsigned dominant=interaction.settings.leftHanded?0:1;
    const auto& hand=t.hands[dominant];
    bool pressed=hand.tracked&&(hand.trigger>.6f||hand.primary),back=hand.tracked&&hand.secondary;
    if(open) {
        if(back&&!cancel)open=false;
        const auto stick=menuStick(t);
        float x=stick[0],y=stick[1];bool edit=false;int direction=1;
        if(std::abs(x)<.4f&&std::abs(y)<.4f)repeatAt=0;
        if(t.seconds>=repeatAt&&(std::abs(x)>.6f||std::abs(y)>.6f)) {
            if(std::abs(y)>std::abs(x))row=(row+(y>0?5:1))%6;
            else {edit=true;direction=x>0?1:-1;}
            repeatAt=t.seconds+.25;
        }
        if(pressed&&!trigger) {
            Pose aim=interaction.localPose(hand.aim);Vec3 ray=rotate(aim.orientation,{0,0,-1});
            if(ray.z<-.01f) {
                float distance=(-1.6f-aim.position.z)/ray.z;Vec3 hit=aim.position+ray*distance;
                float top=interaction.settings.eyeHeight+.24f;
                int pointed=int(std::floor((top+.045f-hit.y)/.11f));
                if(distance>0&&std::abs(hit.x)<.7f&&pointed>=0&&pointed<6){row=pointed;direction=hit.x<0?-1:1;}
            }
            edit=true;
        }
        if(edit) {
            auto& s=interaction.settings;
            if(row==0)s.leftHanded=!s.leftHanded;
            if(row==1){s.eyeHeight=std::clamp(s.eyeHeight+direction*.05f,.5f,2.2f);recenterRequested=true;}
            if(row==2)s.renderScale=std::clamp(s.renderScale+direction*.1f,.5f,1.5f);
            if(row==3)s.throwStrength=std::clamp(s.throwStrength+direction*.1f,.25f,2.f);
            if(row==4)recenterRequested=true;
            if(row==5)open=false;
            {std::lock_guard lock(snap::settings_mutex());snap::settings().vr=s;}
            snap::settings_mark_dirty();
        }
    }
    trigger=pressed;cancel=back;return opened;
}
}
