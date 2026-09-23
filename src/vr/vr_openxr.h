#pragma once
#include "vr_interaction.h"
#include <memory>
#include <string>
struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12Resource;
namespace snap::vr {
class OpenXR {
public:
    OpenXR(); ~OpenXR();
    OpenXR(const OpenXR&)=delete;
    OpenXR& operator=(const OpenXR&)=delete;
    bool initialize(ID3D12Device*,ID3D12CommandQueue*,float renderScale,std::string& error);
    bool begin(Tracking&);
    ID3D12Resource* acquire(unsigned eye);
    void release(unsigned eye);
    void end(bool rendered);
    void haptic(unsigned hand,float strength=0.3f);
    unsigned width(unsigned eye) const;
    unsigned height(unsigned eye) const;
    int64_t format() const;
    bool shouldRender() const;
    bool needsRestart() const;
    unsigned refreshRate() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
