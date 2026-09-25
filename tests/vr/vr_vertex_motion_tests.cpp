#include "../../src/vr/vr_vertex_motion.h"
#include "../../src/vr/vr_animation_clock.h"
#include <cstdlib>
#include <iostream>
static void check(bool v,const char* message){if(!v){std::cerr<<message<<'\n';std::exit(1);}}
int main() {
    using namespace snap::vr;
    const std::vector<float> a{0,0,0, 10,0,0, 0,10,0, 10,10,0};
    std::vector<float> b=a;b[8]=2;b[11]=2;
    const std::vector<float> uv{0,0, 1,0, 0,1, 1,1};
    const VertexTopology topology{{0,1,2,2,1,3},true};
    std::vector<float> velocity(a.size());
    const auto motion=vertexMotion({b,uv,topology},{a,uv,topology},velocity);
    check(motion.matched&&motion.moving==2&&motion.maxDelta==2,"bending mesh must produce two moving vertices");
    for(unsigned hz:{72u,80u,90u}) {
        AnimationClock clock;
        float last=-1;
        for(unsigned i=0;i<hz/30+1;++i) {
            auto sample=clock.sample(1,1,1+1.0/30,10+double(i)/hz);
            // Exact RSPProcessCS local-position expression, checked against
            // the authored bending trajectory, not just against its endpoints.
            const float z=b[8]-velocity[8]*(1-sample.alpha);
            const float expected=float(std::min(double(i)/hz*60,2.0));
            check(std::abs(z-expected)<1e-4f&&z>last,"bending vertex must advance at display cadence");last=z;
        }
    }
    auto changed=topology;changed.corners={0,2,1,2,3,1};
    check(!vertexMotion({b,uv,changed},{a,uv,topology},velocity).matched,"equal count with changed topology must not match");
    auto reorderedUV=uv;std::swap(reorderedUV[0],reorderedUV[2]);
    check(!vertexMotion({b,reorderedUV,topology},{a,uv,topology},velocity).matched,"reordered vertices must not match");
    auto teleport=b;teleport[8]=1000;
    std::fill(velocity.begin(),velocity.end(),0);
    check(!vertexMotion({teleport,uv,topology},{a,uv,topology},velocity).matched,"large discontinuity must snap");
    check(std::all_of(velocity.begin(),velocity.end(),[](float v){return v==0;}),"rejected pair must not partially write velocities");
    check(vertexMotion({b,uv,topology},{a,uv,topology},velocity).matched,"next continuous pair must recover immediately");
    const std::vector<uint16_t> owners{0,0,0,1,1,1};
    const std::vector<uint32_t> starts{0,3},faces{0,1,2,3,4,5};
    auto local=vertexTopology(owners,starts,faces);
    check(local[0].corners==local[1].corners&&local[0].valid&&local[1].valid,"heap offsets must not change topology identity");
    const std::vector<uint32_t> mixed{0,1,3};
    local=vertexTopology(owners,starts,mixed);
    check(!local[0].valid&&!local[1].valid,"mixed-limb geometry must not guess correspondence");
    std::cout<<"Vertex motion tests passed\n";
}
