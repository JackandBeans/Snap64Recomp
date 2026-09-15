//
// RT64
//

#pragma once

#include "hle/rt64_game_frame.h"
#include "hle/rt64_snap_vr.h"

#include "rt64_buffer_uploader.h"

namespace RT64 {
    struct ProjectionProcessor {
        std::unique_ptr<BufferUploader> bufferUploader;
        std::vector<BufferUploader::Upload> uploads;

        struct ProcessParams {
            RenderWorker *worker = nullptr;
            WorkloadQueue *workloadQueue = nullptr;
            GameFrame *curFrame = nullptr;
            const GameFrame *prevFrame = nullptr;
            float curFrameWeight = 1.0f;
            float prevFrameWeight = 0.0f;
            float aspectRatioScale = 1.0f;
            // Pokemon Snap port, the headset: when set, the eyes' transforms
            // are built as well (rt64_snap_vr.h).
            const SnapVR::EyeFrame *snapVrEyes = nullptr;
            const SnapVR::GameCamera *snapVrCamera = nullptr;
            uint32_t snapVrVirtualWidth = 0;
            uint32_t snapVrVirtualHeight = 0;
        };

        ProjectionProcessor();
        ~ProjectionProcessor();
        void setup(RenderWorker *worker);
        void process(const ProcessParams &p);
        void processScene(const ProcessParams &p, const GameScene &scene, size_t sceneIndex);
        void snapVrProcess(const ProcessParams &p);
        void upload(const ProcessParams &p);
    };
};