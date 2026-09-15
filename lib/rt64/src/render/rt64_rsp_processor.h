//
// RT64
//

#pragma once

#include "hle/rt64_workload.h"

#include "rt64_descriptor_sets.h"

namespace RT64 {
    struct RSPProcessor {
        struct ProcessCB {
            uint32_t vertexStart;
            uint32_t vertexCount;
            float prevFrameWeight;
            float curFrameWeight;
        };

        struct ModifyCB {
            uint32_t modifyCount;
        };

        struct ProcessParams {
            RenderWorker *worker = nullptr;
            DrawData *drawData = nullptr;
            DrawBuffers *drawBuffers = nullptr;
            OutputBuffers *outputBuffers = nullptr;
            float prevFrameWeight = 0.0f;
            float curFrameWeight = 1.0f;
        };

        ProcessCB processCB;
        ModifyCB modifyCB;
        std::unique_ptr<RSPProcessDescriptorSet> processSet;
        // Pokemon Snap port, the headset: the same bindings with an eye's
        // view-projections and viewports in place of the picture's
        // (rt64_snap_vr.h), one set per eye so the recorded dispatches keep
        // their own.
        std::unique_ptr<RSPProcessDescriptorSet> snapVrProcessSet[2];
        std::unique_ptr<RSPModifyDescriptorSet> modifySet;

        RSPProcessor(RenderDevice *device);
        ~RSPProcessor();
        void process(const ProcessParams &p);
        void recordCommandList(RenderWorker *worker, const ShaderLibrary *shaderLibrary, const OutputBuffers *outputBuffers, int32_t snapVrEye = -1);
    };
};
