/**
 * @file rsp_microcode.cpp
 * @brief RSP microcode dispatch implementation for Snap64 Recomp.
 *
 * Pokemon Snap uses two RSP task types:
 *   - M_GFXTASK (1): Graphics display list processing (F3DEX / Fast3D variant)
 *   - M_AUDTASK (2): Audio processing
 *
 * The graphics tasks are handled by RT64 through the HLE renderer (the display
 * list is submitted via RendererContext::send_dl, so we don't need a separate
 * RSP microcode for GFX tasks). Audio RSP tasks use the N64 audio microcode.
 *
 * This callback is invoked by librecomp's RSP task runner to determine which
 * recompiled microcode function should handle a given task.
 */

#include "rsp_microcode.h"
#include "librecomp/rsp.hpp"

#include <cstdio>
#include "ultramodern/ultra64.h"

// RSP microcode function declarations.
// These are provided by the recompiled RSP microcode or by librecomp built-ins.
// Forward-declare the ones we expect to use.

// N64 audio microcode — provided by librecomp as a built-in.
RspExitReason aspMain(uint8_t* rdram, uint32_t ucode_addr);

namespace snap {

RspUcodeFunc* get_rsp_microcode(const OSTask* task) {
    // OSTask type field at task->t.type
    switch (task->t.type) {
        case M_GFXTASK:
            // Graphics tasks are handled via HLE by RT64.
            // Return nullptr to signal that the RSP runner should skip this task
            // (it will be picked up by the renderer thread via send_dl instead).
            return nullptr;

        case M_AUDTASK:
            // Audio microcode — the standard N64 audio task processor.
            return aspMain;

        default:
            fprintf(stderr, "[SNAP-RSP] Unknown RSP task type: %d (ucode_boot=0x%08X)\n",
                    task->t.type, task->t.ucode_boot);
            return nullptr;
    }
}

} // namespace snap
