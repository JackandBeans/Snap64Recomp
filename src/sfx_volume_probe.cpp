/**
 * @file sfx_volume_probe.cpp
 * @brief Logs each volume pushed to a sound-player voice, so the Effects
 *        slider can be checked against what the voice was actually told.
 *
 * alSndpSetVol(sndp, vol) is the only way a started voice's volume changes:
 * auSetSoundVolume (patches/src/sfx_volume_patch.c), auSetSoundGlobalVolume
 * and auSetCurrentSoundsGlobalVolume all end here. A voice's FIRST volume
 * never passes through it -- the audio thread writes that straight into the
 * voice state from the slot the three play functions filled -- so this sees
 * exactly the writes the Effects slider used to miss. sndp->target (+0x3C)
 * names the voice. Reads a0/a1 and one word of game memory; changes nothing.
 * Prints only under SNAP_STATS.
 */

#include <cstdint>
#include <cstdio>

#include "hle/rt64_snap_diag.h"
#include "recomp.h"

extern "C" {
#include "funcs.h"
}

extern "C" void alSndpSetVol(uint8_t* rdram, recomp_context* ctx) {
    if (snapdiag::statsEnabled()) {
        const int32_t target = static_cast<int32_t>(MEM_W(0x3C, ctx->r4));
        const int16_t vol = static_cast<int16_t>(ctx->r5);
        printf("[SNAP-SFX] voice=%d vol=%d\n", target, vol);
    }
    __real_alSndpSetVol(rdram, ctx);
}
