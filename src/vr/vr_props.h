#pragma once
#include "vr_interaction.h"
#include <memory>
#include "vr_gpu.h"
namespace snap::vr {
class Props {
public:
    Props(VRDevice*,VRQueue*);
    ~Props();
    std::array<FluteContact,2> handContacts(const Tracking&,const Interaction&);
    void draw(VRTexture* color,VRTexture* depth,VRTexture* screen,
              Pose eye,Fov fov,const GameState&,const InteractionFrame&,const Tracking&,const Interaction&,bool focus,bool optionsOpen=false,int optionsRow=0);
    void copy(VRTexture* source,VRTexture* destination,unsigned width,unsigned height);
    void beginTiming();
    double endTiming();
    double fenceWaitMs() const;
    void capture(VRTexture* source,const char* filename);
    void presentation(VRTexture* color,Pose calibratedEye,Fov fov,float gain,bool portal,float eyeHeight);
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
}
