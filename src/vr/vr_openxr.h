#pragma once
#include "vr_interaction.h"
#include <memory>
#include <string>
#include "vr_gpu.h"
namespace snap::vr {
class OpenXR {
public:
    OpenXR(); ~OpenXR();
    OpenXR(const OpenXR&)=delete;
    OpenXR& operator=(const OpenXR&)=delete;
    bool initialize(VRDevice*,VRQueue*,float renderScale,std::string& error);
    bool begin(Tracking&);
    VRTexture* acquire(unsigned eye);
    void release(unsigned eye);
    void end(bool rendered);
    void haptic(unsigned hand,float strength=0.3f);
    unsigned width(unsigned eye) const;
    unsigned height(unsigned eye) const;
    int64_t format() const;
    bool shouldRender() const;
    bool needsRestart() const;
    unsigned refreshRate() const;
    float physicalRefreshRate() const;
    // CPU elapsed wait/begin/end; wait is pacing, not render work.
    std::array<double,3> pacingTimesMs() const;
    double predictedMonotonicSeconds() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
