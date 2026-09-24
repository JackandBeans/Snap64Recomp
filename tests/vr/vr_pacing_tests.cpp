#include "../../lib/rt64/src/hle/rt64_snap_vr_pacing.h"
#include <cstdlib>
#include <iostream>

static void check(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

int main() {
    // Use integer units that exactly represent both 30 Hz and 72 Hz.
    // Source frames arrive every 12 units, display samples take 5 units.
    RT64::SnapVRPacing pacing;
    int64_t now = 1000;
    unsigned rendered = 0;
    unsigned logicalTicks = 0, displayTicks = 0;
    for (unsigned source = 0; source < 300; ++source) {
        now = std::max(now, int64_t(1000 + source * 12));
        pacing.begin(now, 12, 5);
        logicalTicks += 72;
        const unsigned frames = (logicalTicks - displayTicks) / 30;
        for (unsigned frame = 0; frame < frames; ++frame) {
            now += 5;
            ++rendered;
            if (!pacing.canRenderAnother(now)) break;
        }
        displayTicks += frames * 30;
    }
    check(rendered == 720, "30 Hz source must sustain all 720 display samples at 72 Hz over ten seconds");
    check(now <= 4610, "fractional cadence must not accumulate latency");

    // 240 units/second represents 30 Hz sources and 80 Hz display exactly.
    pacing.reset();now=1000;rendered=0;logicalTicks=displayTicks=0;
    for(unsigned source=0;source<300;++source) {
        now=std::max(now,int64_t(1000+source*8));
        pacing.begin(now,8,3);
        logicalTicks+=80;
        const unsigned frames=(logicalTicks-displayTicks)/30;
        for(unsigned frame=0;frame<frames;++frame) {
            now+=3;++rendered;
            if(!pacing.canRenderAnother(now))break;
        }
        displayTicks+=frames*30;
    }
    check(rendered==800,"30 Hz source must sustain 800 display samples at 80 Hz over ten seconds");
    check(now<=3406,"80 Hz cadence must not accumulate latency");

    pacing.reset();
    pacing.begin(1000, 33333, 13889);
    check(pacing.canRenderAnother(12100), "spare time permits an intermediate animation pose");
    check(!pacing.canRenderAnother(34333), "no further stale poses after the source deadline");
    // First interval overran by 8 ms: the next one gets 25 ms, not 33 ms.
    pacing.begin(42333, 33333, 13889);
    check(!pacing.canRenderAnother(67666), "previous overrun must be paid back");

    // Long stalls and suspend/resume discard timing debt, and cannot bank
    // seconds of interpolation work for the next workload.
    pacing.begin(5000000, 33333, 13889);
    check(!pacing.canRenderAnother(5000001), "hitch recovery prioritizes the next source frame");
    pacing.begin(5100000, 33333, 13889);
    check(!pacing.canRenderAnother(5100001), "persistent overload cannot grow interpolation windows");
    pacing.reset();
    pacing.begin(6000000, 33333, 13889);
    pacing.begin(6100000, 33333, 11111);
    check(pacing.canRenderAnother(6111111), "refresh-rate changes reset the previous cadence");
    std::cout << "VR pacing tests passed\n";
}
