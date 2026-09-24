#include "../../src/vr/vr_animation_clock.h"
#include "../../src/vr/vr_snapshot_window.h"
#include <cstdlib>
#include <iostream>

static void check(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
static bool close(double a,double b){return std::abs(a-b)<1e-5;}
int main(){
    for(unsigned sourceHz:{30u,60u})for(unsigned hz:{72u,80u,90u}) {
        snap::vr::AnimationClock clock;
        // A source pose reaches the renderer after a fixed pipeline delay.
        // Neither that delay nor fractional display cadence may collapse poses
        // to the latest endpoint on every intermediate display interval.
        constexpr double origin=10,latency=.052;
        const double period=1.0/sourceHz;
        double last=-1;
        unsigned intermediate=0;
        for(unsigned frame=0;frame<hz*10;++frame) {
            const double elapsed=double(frame)/hz;
            const double source=origin+std::floor(elapsed*sourceHz+1e-7)*period;
            const auto sample=clock.sample(1,source-period,source,origin+latency+elapsed,period);
            check(close(sample.time,origin+elapsed-period),"pose time must follow every display interval");
            check(sample.time>last,"moving animation must progress each display frame");
            check(!sample.stale,"on-time source must not produce stale animation");
            if(sample.alpha>1e-5&&sample.alpha<1-1e-5)++intermediate;
            // An authored constant-velocity vertex must follow the selected
            // sample rather than snap to either 30 Hz source endpoint.
            const double vertexA=(source-period)*3,vertexB=source*3;
            check(close(vertexA+(vertexB-vertexA)*sample.alpha,sample.time*3),"deforming vertex sample time mismatch");
            last=sample.time;
        }
        check(intermediate>hz*5,"most display frames need intermediate poses");
    }
    snap::vr::AnimationClock clock;
    // Early publications must not replace the pair bracketing the delayed
    // presentation time. Exercise jitter that the ideal cadence test misses.
    for(unsigned hz:{72u,80u,90u}) {
        snap::vr::AnimationClock jitterClock;
        snap::vr::SnapshotWindow<int> window;
        double newest=10,lastPose=-1;
        int sequence=0;
        window.publish(std::make_shared<int>(sequence),1,newest-1.0/30,newest);
        for(unsigned i=0;i<4*hz;++i) {
            const double elapsed=double(i)/hz;
            const int available=int(std::floor((elapsed+(i%3==0?.010:0))*30));
            while(sequence<available){++sequence;newest=10+sequence/30.0;window.publish(std::make_shared<int>(sequence),1,newest-1.0/30,newest);}
            const double target=jitterClock.target(1,newest,20+elapsed);
            const auto chosen=window.select(target);
            const double b=10+*chosen/30.0;
            const auto pose=jitterClock.sample(1,b-1.0/30,b,20+elapsed);
            check(close(pose.time,10+elapsed-1.0/30),"early publication selected the wrong interpolation pair");
            check(pose.time>lastPose,"jittered publication froze a moving pose");
            check(window.size()<=4,"presentation history grew without bound");
            lastPose=pose.time;
        }
        // In-flight references survive eviction and reset; release only when
        // the frame that retained them completes.
        auto retained=window.select(0);
        std::weak_ptr<int> lifetime=retained;
        window.publish(std::make_shared<int>(999),2,0,1);
        check(window.size()==1&&*window.select(0)==999,"epoch reset retained an old pair");
        check(!lifetime.expired(),"in-flight snapshot destroyed during epoch reset");
        retained.reset();check(lifetime.expired(),"retired snapshot leaked after reset");
        window.clear();check(!window.select(1),"session reset retained snapshot state");
    }
    const auto first=clock.sample(1,9,10,20,1);
    check(close(first.time,9),"one source interval of presentation delay");
    const auto held=clock.sample(1,9,10,25,1);
    check(held.stale&&close(held.time,10),"missing sources must clamp without extrapolation");
    const double offset=clock.presentationOffset();
    const auto recovered=clock.sample(1,14,15,25.25,1);
    check(close(recovered.time,14.25)&&close(clock.presentationOffset(),offset),"recovery must not accumulate latency");
    check(close(clock.sample(1,14,15,25.20,1).time,14.25),"clock regression must not rewind motion");
    const auto changed=clock.sample(1,14,15,25.25+1.0/72,1);
    check(changed.time>recovered.time,"refresh changes must not reset the animation phase");
    check(clock.sample(1,15,15,26).stale,"degenerate source pair must be rejected");
    const auto epoch=clock.sample(2,1,2,100,1);
    check(close(epoch.time,1),"scene change must reset the old epoch");
    clock.reset();check(close(clock.sample(2,3,4,200,1).time,3),"resume must discard the previous clock anchor");
    std::cout<<"Animation clock tests passed\n";
}
