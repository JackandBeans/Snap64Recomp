#pragma once
#include "vr_math.h"

namespace snap::vr {
// Conservative pixel bounds of the opening movie's existing opaque surround.
// These constants match shaders/quest/presentation.hlsl. Only pixels that the
// presentation pass makes completely black are excluded; eye resolution and
// the visible movie, including its feathered edge, are unchanged.
inline std::array<int,4> portalScissor(Pose eye,Fov fov,float height,int width,int imageHeight) {
    const std::array<int,4> full{0,0,width,imageHeight};
    const float left=std::tan(fov.left),right=std::tan(fov.right);
    const float up=std::tan(fov.up),down=std::tan(fov.down);
    float minX=float(width),minY=float(imageHeight),maxX=0,maxY=0;
    for(float x:{-.95f,.95f})for(float y:{-.65f,.65f}) {
        const auto p=rotate(conjugate(eye.orientation),Vec3{x,y+height,-1.6f}-eye.position);
        // A portal crossing the eye plane cannot use a finite projected box.
        if(p.z>=-.001f)return full;
        const float px=(p.x/-p.z-left)/(right-left)*width;
        const float py=(up-p.y/-p.z)/(up-down)*imageHeight;
        if(!std::isfinite(px)||!std::isfinite(py))return full;
        minX=std::min(minX,px);maxX=std::max(maxX,px);
        minY=std::min(minY,py);maxY=std::max(maxY,py);
    }
    return {int(std::clamp(std::floor(minX)-2.f,0.f,float(width))),
        int(std::clamp(std::floor(minY)-2.f,0.f,float(imageHeight))),
        int(std::clamp(std::ceil(maxX)+2.f,0.f,float(width))),
        int(std::clamp(std::ceil(maxY)+2.f,0.f,float(imageHeight)))};
}
}
