#include "../../src/vr/vr_portal.h"
#include <cstdlib>
#include <iostream>
using namespace snap::vr;
static void check(bool condition){if(!condition){std::cerr<<"Portal clipped a visible pixel\n";std::exit(1);}}
int main() {
    // Sample the actual presentation shader's ray-plane mask, independently
    // of the projected-corner optimization. Test asymmetric eyes and tracking.
    const Fov fov{-.85f,.72f,.81f,-.77f};
    const int width=1680,height=1760;
    const float eyeHeight=1.2f;
    for(float heading:{-1.4f,-.4f,0.f,.6f,1.4f})for(float pitch:{-.8f,0.f,.7f})for(float z:{-.1f,-1.59f,-1.8f,.2f}) {
        Pose eye{yaw(heading)*Quat{std::sin(pitch/2),0,0,std::cos(pitch/2)},{.032f,1.31f,z}};
        const auto bounds=portalScissor(eye,fov,eyeHeight,width,height);
        for(int y=0;y<height;y+=7)for(int x=0;x<width;x+=7) {
            const float u=(x+.5f)/width,v=(y+.5f)/height;
            const auto ray=rotate(eye.orientation,{std::tan(fov.left)+(std::tan(fov.right)-std::tan(fov.left))*u,
                std::tan(fov.up)+(std::tan(fov.down)-std::tan(fov.up))*v,-1});
            const float distance=(-1.6f-eye.position.z)/std::min(ray.z,-.00001f);
            const auto hit=eye.position+ray*distance;
            const bool visible=ray.z<0&&distance>0&&std::abs(hit.x)<.95f&&std::abs(hit.y-eyeHeight)<.65f;
            if(visible)check(x>=bounds[0]&&x<bounds[2]&&y>=bounds[1]&&y<bounds[3]);
        }
    }
    const auto centered=portalScissor({{}, {0,eyeHeight,0}},Fov{},eyeHeight,width,height);
    check((centered[2]-centered[0])*(centered[3]-centered[1])<width*height/3);
    std::cout<<"Portal visibility tests passed\n";
}
