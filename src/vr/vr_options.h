#pragma once
#include "vr_interaction.h"
namespace snap::vr {
// A small host panel available without modifying the ROM's menu scripts.
class Options {
public:
    bool open=false, recenterRequested=false;
    int row=0;
    bool update(const Tracking& t,Interaction& interaction);
private:
    bool click=false, trigger=false, cancel=false, bothLatch=false;
    double repeatAt=0;
};
}
