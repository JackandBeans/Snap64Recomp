#include "hle/rt64_rigid_body.h"
#include "../include/rt64_extended_gbi.h"
#include "../../src/vr/vr_animation_clock.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

static void check(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
static hlslpp::float4x4 pose(double time,double origin=0) {
    const float angle=float(time*.7),c=std::cos(angle),s=std::sin(angle);
    return hlslpp::float4x4(c,s,0,0,-s,c,0,0,0,0,1,0,float(time*3-origin),0,0,1);
}
static void prepare(RT64::RigidBody& body,const hlslpp::float4x4& a,const hlslpp::float4x4& b,uint8_t translation=G_EX_COMPONENT_INTERPOLATE) {
    body.updateLinear(a,b,translation);
    body.updateAngular(a,b,G_EX_COMPONENT_INTERPOLATE,G_EX_COMPONENT_INTERPOLATE,G_EX_COMPONENT_INTERPOLATE);
    body.updatePerspective(a,b,G_EX_COMPONENT_SKIP);
    body.updateDecomposition(a,b,true);
}
int main() {
    for(unsigned sourceHz:{30u,60u})for(unsigned hz:{72u,80u}) {
        snap::vr::AnimationClock clock;
        RT64::RigidBody body;
        for(unsigned i=0;i<hz*5;++i) {
            const double period=1.0/sourceHz;
            const double elapsed=double(i)/hz,b=1+std::floor(elapsed*sourceHz+1e-7)*period,a=b-period;
            const auto sample=clock.sample(1,a,b,10+elapsed,period);
            const auto prev=pose(a),cur=pose(b),expected=pose(sample.time);
            prepare(body,prev,cur);
            const auto actual=body.lerp(sample.alpha,prev,cur,true);
            // Uses the renderer's real decomposition/slerp, including scalar
            // math as on Android. Verify a rotating limb with translation.
            for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col)
                check(std::abs(float(actual[row][col]-expected[row][col]))<2e-4,"interpolated limb differs from analytic motion");
        }
    }
    RT64::RigidBody body;
    auto a=pose(1),b=pose(1+1.0/30,1000);
    // A block rebase must translate A into B's origin before decomposition.
    a[3][0]-=1000;
    prepare(body,a,b);
    const auto rebased=body.lerp(.5f,a,b,true),expected=pose(1+1.0/60,1000);
    check(std::abs(float(rebased[3][0]-expected[3][0]))<2e-4,"rebase lost the corrected previous transform");
    // An explicit per-object cut must snap that object, then recover on the
    // next continuous pair without retaining a cut latch on unrelated limbs.
    prepare(body,a,b,G_EX_COMPONENT_SKIP);
    const auto cut=body.lerp(.25f,a,b,true);
    check(float(cut[3][0])==float(b[3][0]),"explicit translation cut blended");
    prepare(body,a,b);
    check(std::abs(float(body.lerp(.5f,a,b,true)[3][0]-expected[3][0]))<2e-4,"cut did not recover");
    std::cout<<"RT64 limb pose tests passed\n";
}
