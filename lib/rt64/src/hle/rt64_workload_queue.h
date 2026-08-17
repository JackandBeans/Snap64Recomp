//
// RT64
//

#pragma once

#include <array>

#include "common/rt64_enhancement_configuration.h"
#include "common/rt64_profiling_timer.h"
#include "common/rt64_user_configuration.h"
#include "render/rt64_framebuffer_renderer.h"
#include "render/rt64_projection_processor.h"
#include "render/rt64_raster_shader_cache.h"
#include "render/rt64_tile_processor.h"
#include "render/rt64_look_at_processor.h"
#include "render/rt64_transform_processor.h"

#include "rt64_shared_queue_resources.h"
#include "rt64_workload.h"

#if RT_ENABLED
#   include "render/rt64_raytracing_shader_cache.h"
#endif

#define WORKLOAD_QUEUE_SIZE 4

namespace RT64 {
    struct PresentQueue;

    struct WorkloadQueue {
        struct External {
            RenderDevice *device = nullptr;
            RenderWorker *workloadGraphicsWorker = nullptr;
            BufferUploader *workloadExtrasUploader = nullptr;
            BufferUploader *workloadVelocityUploader = nullptr;
            BufferUploader *workloadTilesUploader = nullptr;
            PresentQueue *presentQueue = nullptr;
            SharedQueueResources *sharedResources = nullptr;
            RasterShaderCache *rasterShaderCache = nullptr;
            TextureCache *textureCache = nullptr;
            const ShaderLibrary *shaderLibrary = nullptr;
            UserConfiguration::GraphicsAPI createdGraphicsAPI = UserConfiguration::GraphicsAPI::OptionCount;
#       if RT_ENABLED
            const RenderTexture *blueNoiseTexture = nullptr;
            RaytracingShaderCache *rtShaderCache = nullptr;
#       endif
        };

        struct WorkloadConfiguration {
            hlslpp::float2 resolutionScale = 1.0f;
            uint32_t downsampleMultiplier = 1;
            bool raytracingEnabled = false;
            float aspectRatioSource = 1.0f;
            float aspectRatioTarget = 1.0f;
            float aspectRatioScale = 1.0f;
            float extAspectPercentage = 1.0f;
            uint32_t targetRate = 0;
            bool postBlendNoise = false;
            bool postBlendNoiseNegative = false;
        };

        External ext;
        // Pokemon Snap port: conservative frame interpolation. The game keeps
        // its camera in the modelview stack (the projection stack is a static
        // perspective), so world transform matching is what makes panning
        // smooth — but the per-frame rebuilt display heap and rows of
        // identical vegetation quads let the call-hash matcher pair draws
        // across the screen and lerp garbage. When set, GameFrame:
        //  - estimates the camera's frame delta from a consensus of
        //    unique-content transforms (terrain chunks outvote moving set
        //    pieces) and interpolates ONLY transforms that land exactly where
        //    that delta predicts them: the static world glides with the
        //    camera at any pan speed, while anything with motion of its own
        //    (actors) renders whole at the game's native 20fps — per-limb
        //    gating would tear animating models apart,
        //  - prefers identical-vertex-content pairs, reuses the last camera
        //    delta when no anchor is in view (animated water), and treats an
        //    anchor delta that is both implausibly large AND discontinuous
        //    with the previous delta as a hard camera cut, presenting those
        //    frames unlerped instead of smearing (fast-but-continuous pans
        //    keep interpolating), and
        //  - never computes per-vertex velocities (heap reordering makes
        //    equal-count vertex ranges pair misaligned geometry); the
        //    zero-filled buffers uploaded at submission remain in effect.
        bool snapCameraOnlyInterpolation = false;
        // Pokemon Snap port: the last camera frame delta estimated from a
        // valid anchor, reused on frames whose view contains no static
        // unique-content geometry (e.g. looking down at the CPU-animated
        // water). A stale delta self-corrects: wrong predictions fall outside
        // the tight gate and simply present unlerped. Only written and read
        // by the workload thread (GameFrame::matchScene).
        hlslpp::float4x4 snapLastViewDelta;
        bool snapLastViewDeltaValid = false;
        // The previous frame's raw anchor delta, recorded even when it was
        // not trusted. Continuity against this is what lets a sustained fast
        // pan (the scripted intro swing) re-qualify for interpolation one
        // frame after a cut instead of cascading into cuts for the whole pan.
        hlslpp::float4x4 snapPrevViewDelta;
        bool snapPrevViewDeltaValid = false;
        std::array<Workload, WORKLOAD_QUEUE_SIZE> workloads;
        int threadCursor;
        int writeCursor;
        int barrierCursor;
        std::mutex cursorMutex;
        std::condition_variable cursorCondition;
        uint64_t workloadId;
        uint64_t lastPresentId;
        std::mutex workloadIdMutex;
        std::condition_variable workloadIdCondition;
        std::thread *renderThread = nullptr;
        std::thread *idleThread = nullptr;
        bool idleActive = false;
        std::mutex idleMutex;
        std::condition_variable idleCondition;
        std::mutex workerMutex;
        std::mutex threadMutex;
        std::atomic<bool> threadsRunning = false;
        std::atomic<bool> rtEnabled = false;
        std::atomic<bool> ubershadersOnly = false;
        std::atomic<bool> ubershadersVisible = false;
        std::unique_ptr<FramebufferRenderer> framebufferRenderer;
        std::unique_ptr<RenderFramebufferManager> renderFramebufferManager;
        TileProcessor tileProcessor;
        LookAtProcessor lookAtProcessor;
        TransformProcessor transformProcessor;
        ProjectionProcessor projectionProcessor;
        std::unique_ptr<RSPProcessor> rspProcessor;
        std::unique_ptr<VertexProcessor> vertexProcessor;
        std::unique_ptr<RenderTarget> dummyDepthTarget;
        std::unique_ptr<RenderQueryPool> queryPool;
        FramebufferChangePool scratchFbChangePool;
        ProfilingTimer rendererCPUProfiler = ProfilingTimer(120);
        ProfilingTimer rendererGPUProfiler = ProfilingTimer(120);
        ProfilingTimer matchingProfiler = ProfilingTimer(120);
        ProfilingTimer workloadProfiler = ProfilingTimer(120);
        std::array<GameFrame, 2> gameFrames;
        uint32_t prevFrameIndex = uint32_t(gameFrames.size()) - 1;
        uint32_t curFrameIndex = 0;

        WorkloadQueue();
        ~WorkloadQueue();
        void reset();
        void advanceToNextWorkload();
        void repeatLastWorkload();
        uint32_t previousWriteCursor() const;
        void waitForIdle();
        void waitForWorkloadId(uint64_t waitId);
        void setup(const External &ext);
        void updateMultisampling();
        void threadConfigurationUpdate(hlslpp::uint2 viFbSize, WorkloadConfiguration &workloadConfig);
        void threadConfigurationValidate();
        void threadRenderFrame(GameFrame &curFrame, const GameFrame &prevFrame, const WorkloadConfiguration &workloadConfig,
            const DebuggerRenderer &debuggerRenderer, const DebuggerCamera &debuggerCamera, float curFrameWeight, float prevFrameWeight,
            float deltaTimeMs, RenderTargetKey overrideTargetKey, int32_t overrideTargetFbPairIndex, RenderTarget *overrideTarget,
            uint32_t overrideTargetModifier, bool uploadVelocity, bool uploadExtras, bool interpolateTiles, bool interpolateLookAts);

        void threadAdvanceBarrier();
        void threadAdvanceWorkloadId(uint64_t newWorkloadId);
        void renderThreadLoop();
        void idleThreadLoop();
    };
};