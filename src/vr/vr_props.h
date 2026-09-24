#pragma once
#include "vr_interaction.h"
#include <memory>
struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12Resource;
namespace snap::vr {
class Props {
public:
    Props(ID3D12Device*,ID3D12CommandQueue*);
    ~Props();
    void draw(ID3D12Resource* color,ID3D12Resource* depth,ID3D12Resource* screen,
              Pose eye,Fov fov,const GameState&,const InteractionFrame&,const Tracking&,const Interaction&,bool focus,bool optionsOpen=false,int optionsRow=0);
    void copy(ID3D12Resource* source,ID3D12Resource* destination,unsigned width,unsigned height);
    void beginTiming();
    double endTiming();
    double fenceWaitMs() const;
    void capture(ID3D12Resource* source,const char* filename);
    void presentation(ID3D12Resource* color,Pose calibratedEye,Fov fov,float gain,bool portal,float eyeHeight);
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
}
