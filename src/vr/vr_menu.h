#pragma once
#include "vr_math.h"
namespace snap::vr {
struct MenuPoint {float x=0,y=0;bool valid=false;};
struct MenuRect {
    float x=0,y=0,w=0,h=0;
    bool contains(MenuPoint p)const{return p.valid&&p.x>=x-3&&p.x<=x+w+3&&p.y>=y-3&&p.y<=y+h+3;}
    unsigned direction(MenuPoint p)const {
        if(!p.valid||contains(p))return 0;
        // A side-button list must not follow a pointer over the photograph.
        if(x<35&&w<80&&p.x>95)return 0;
        if(p.y<y-3)return 0x10000;
        if(p.y>y+h+3)return 0x20000;
        return p.x<x?0x80000:0x40000;
    }
};
struct NameCell {int x=-1,y=-1;bool valid()const{return x>=0&&y>=0;}};
// Original name-entry grid: 5 columns, 19 character rows, then editing actions.
inline NameCell nameCell(float pixelX,float pixelY) {
    if(pixelX<18.5f||pixelX>=83.5f||pixelY<17.f||pixelY>=217.f)return {};
    int y=int((pixelY-17.f)/10.f),x=int((pixelX-18.5f)/13.f);
    if(y==19)x=std::min(x,2); // Backspace, Space, End (wide target).
    return {x,y};
}
inline Pose handMeshPose(Pose controller) {
    constexpr float halfSqrt=.7071067811865475f;
    return compose(controller,Pose{{-halfSqrt,0,0,halfSqrt},{}});
}
}
