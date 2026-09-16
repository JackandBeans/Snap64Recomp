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
        // Pokemon Snap port, the headset: how often the world's camera was
        // the ride's and how often it was not, reported on a timer rather
        // than as a handful of one-shot lines, so the rate is known.
        uint32_t snapVrMatched = 0;
        uint32_t snapVrUnmatched = 0;
        float snapVrWorstEyeDelta = 0.0f;

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
        // False when the frame's world camera is not the ride's: the caller
        // must then draw no eyes, because the world must never be put
        // through the plane meant for flat content (rt64_snap_vr.h).
        bool snapVrProcess(const ProcessParams &p);
        bool snapVrRideFound() const { return snapVrFound; }
        bool snapVrFound = false;
        void upload(const ProcessParams &p);
    };
};