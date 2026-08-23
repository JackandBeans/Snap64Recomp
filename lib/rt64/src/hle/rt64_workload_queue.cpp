//
// RT64
//

#include "rt64_workload_queue.h"

#include "common/rt64_thread.h"

#include "rt64_present_queue.h"

#define ENABLE_HIGH_RESOLUTION_RENDERER 1

#include "rt64_snap_diag.h"

#include <atomic>

// Pokemon Snap port: defined in src/frame_dump.cpp on the game side. Setting
// it asks the game to save the next few frames' framebuffers as images.
// Atomic: armed here on the render thread, consumed on the game thread, read
// by the present queue.
extern "C" std::atomic<int32_t> snap_frame_dump_pending;

namespace RT64 {
    // WorkloadQueue

    WorkloadQueue::WorkloadQueue() {
        reset();
    }

    WorkloadQueue::~WorkloadQueue() {
        threadsRunning = false;
        cursorCondition.notify_all();
        idleCondition.notify_all();

        if (renderThread != nullptr) {
            renderThread->join();
            delete renderThread;
        }

        if (idleThread != nullptr) {
            idleThread->join();
            delete idleThread;
        }

        workloadIdCondition.notify_all();
    }

    void WorkloadQueue::reset() {
        for (Workload &w : workloads) {
            w.reset();
        }

        threadCursor = 0;
        writeCursor = 0;
        barrierCursor = int(workloads.size()) - 1;
        workloadId = 0;
        lastPresentId = 0;
    }

    void WorkloadQueue::advanceToNextWorkload() {
        int nextWriteCursor = (writeCursor + 1) % workloads.size();

        // Stall the thread until the barrier is lifted if we're trying to write on a workload being used by the GPU.
        bool waitForBarrier;
        do {
            const std::scoped_lock lock(cursorMutex);
            waitForBarrier = (nextWriteCursor == barrierCursor);
        } while (waitForBarrier);

        // Modify the cursor and notify anything waiting on the queue.
        {
            const std::scoped_lock lock(cursorMutex);
            writeCursor = nextWriteCursor;
        }

        cursorCondition.notify_all();
    }

    void WorkloadQueue::repeatLastWorkload() {
        {
            const std::scoped_lock lock(cursorMutex);
            threadCursor = previousWriteCursor();
        }

        cursorCondition.notify_all();
    }

    uint32_t WorkloadQueue::previousWriteCursor() const {
        if (writeCursor > 0) {
            return writeCursor - 1;
        }
        else {
            return uint32_t(workloads.size()) - 1;
        }
    }

    void WorkloadQueue::waitForIdle() {
        std::unique_lock<std::mutex> threadLock(threadMutex);
    }

    void WorkloadQueue::waitForWorkloadId(uint64_t waitId) {
        std::unique_lock<std::mutex> workloadLock(workloadIdMutex);
        workloadIdCondition.wait(workloadLock, [&]() {
            return (waitId <= workloadId) || !threadsRunning;
        });
    }

    void WorkloadQueue::setup(const External &ext) {
        this->ext = ext;

        rspProcessor = std::make_unique<RSPProcessor>(ext.device);
        vertexProcessor = std::make_unique<VertexProcessor>(ext.device);
        framebufferRenderer = std::make_unique<FramebufferRenderer>(ext.workloadGraphicsWorker, true, ext.createdGraphicsAPI, ext.shaderLibrary);
        renderFramebufferManager = std::make_unique<RenderFramebufferManager>(ext.device);
        queryPool = ext.device->createQueryPool(2);

        projectionProcessor.setup(ext.workloadGraphicsWorker);
        transformProcessor.setup(ext.workloadGraphicsWorker);
        tileProcessor.setup(ext.workloadGraphicsWorker);
        lookAtProcessor.setup(ext.workloadGraphicsWorker);

        threadsRunning = true;
        renderThread = new std::thread(&WorkloadQueue::renderThreadLoop, this);
        idleThread = new std::thread(&WorkloadQueue::idleThreadLoop, this);
    }

    void WorkloadQueue::updateMultisampling() {
        renderFramebufferManager->destroyAll();
        dummyDepthTarget.reset();
        framebufferRenderer->updateMultisampling();
    }

    void WorkloadQueue::threadConfigurationUpdate(hlslpp::uint2 viFbSize, WorkloadConfiguration &workloadConfig) {
        const std::scoped_lock lock(ext.sharedResources->configurationMutex);
        const bool sizeChanged = ext.sharedResources->swapChainSizeChanged;
        ext.sharedResources->swapChainSizeChanged = false;
        
        // Retrieve the reference height to be used for determining the resolution scale. Impose a minimum in case
        // the game is using too small of a portion of the VI.
        const uint32_t MinimumReferenceHeight = 60;
        const uint32_t referenceHeight = (viFbSize[1] > 0) ? std::max(viFbSize[1], MinimumReferenceHeight) : 240;

        // Compute the aspect ratio to be used for the frame.
        workloadConfig.aspectRatioSource = (viFbSize[1] > 0) ? float(viFbSize[0]) / float(viFbSize[1]) : (4.0f / 3.0f);

        const auto ratioMode = ext.sharedResources->userConfig.aspectRatio;
        switch (ratioMode) {
        case UserConfiguration::AspectRatio::Expand:
            if ((ext.sharedResources->swapChainWidth > 0) && (ext.sharedResources->swapChainHeight > 0)) {
                const float derivedRatioTarget = float(ext.sharedResources->swapChainWidth) / float(ext.sharedResources->swapChainHeight);
                workloadConfig.aspectRatioTarget = std::max(derivedRatioTarget, workloadConfig.aspectRatioSource);
            }
            else {
                workloadConfig.aspectRatioTarget = workloadConfig.aspectRatioSource;
            }

            break;
        case UserConfiguration::AspectRatio::Manual:
            workloadConfig.aspectRatioTarget = float(ext.sharedResources->userConfig.aspectTarget);
            break;
        case UserConfiguration::AspectRatio::Original:
        default:
            workloadConfig.aspectRatioTarget = workloadConfig.aspectRatioSource;
            break;
        }

        // Compute the extended GBI aspect ratio percentage to be used for the frame.
        const auto extRatioMode = ext.sharedResources->userConfig.extAspectRatio;
        switch (extRatioMode) {
        case UserConfiguration::AspectRatio::Expand:
            workloadConfig.extAspectPercentage = 1.0f;
            break;
        case UserConfiguration::AspectRatio::Manual:
            if ((ext.sharedResources->swapChainWidth > 0) && (ext.sharedResources->swapChainHeight > 0)) {
                const float reducedExtTarget = float(ext.sharedResources->userConfig.extAspectTarget) - workloadConfig.aspectRatioSource;
                const float reducedDisplayTarget = workloadConfig.aspectRatioTarget - workloadConfig.aspectRatioSource;
                if ((reducedExtTarget > 0.0f) && (reducedDisplayTarget > 0.0f)) {
                    workloadConfig.extAspectPercentage = std::clamp((reducedExtTarget / reducedDisplayTarget), 0.0f, 1.0f);
                }
                else {
                    workloadConfig.extAspectPercentage = 0.0f;
                }
            }
            else {
                workloadConfig.extAspectPercentage = 0.0f;
            }

            break;
        case UserConfiguration::AspectRatio::Original:
        default:
            workloadConfig.extAspectPercentage = 0.0f;
            break;
        }

        // Compute the resolution scaling to be used for the frame.
        float resolutionMultiplier;
        const auto resolutionMode = ext.sharedResources->userConfig.resolution;
        switch (resolutionMode) {
        case UserConfiguration::Resolution::WindowIntegerScale:
            if (ext.sharedResources->swapChainHeight > 0) {
                resolutionMultiplier = std::max(float((ext.sharedResources->swapChainHeight + referenceHeight - 1) / referenceHeight), 1.0f);
            }
            else {
                resolutionMultiplier = 1.0f;
            }

            break;
        case UserConfiguration::Resolution::Manual:
            resolutionMultiplier = float(ext.sharedResources->userConfig.resolutionMultiplier);
            break;
        case UserConfiguration::Resolution::Original:
        default:
            resolutionMultiplier = 1.0f;
            break;
        }

        uint32_t msaaSampleCount = ext.sharedResources->userConfig.msaaSampleCount();

        // Build the resolution scale vector from the configuration.
        workloadConfig.aspectRatioScale = workloadConfig.aspectRatioTarget / workloadConfig.aspectRatioSource;
        workloadConfig.resolutionScale = { resolutionMultiplier * workloadConfig.aspectRatioScale, resolutionMultiplier };
        workloadConfig.downsampleMultiplier = ext.sharedResources->userConfig.downsampleMultiplier;
        ext.sharedResources->resolutionScale = workloadConfig.resolutionScale;

        // Find the target refresh rate from the configuration.
        const auto refreshRate = ext.sharedResources->userConfig.refreshRate;
        switch (refreshRate) {
        case UserConfiguration::RefreshRate::Display:
            workloadConfig.targetRate = ext.sharedResources->swapChainRate;
            break;
        case UserConfiguration::RefreshRate::Manual:
            workloadConfig.targetRate = ext.sharedResources->userConfig.refreshRateTarget;

            // Limit the target rate to the rate detected by the swap chain.
            if ((ext.sharedResources->swapChainRate > 0) && (workloadConfig.targetRate > ext.sharedResources->swapChainRate)) {
                workloadConfig.targetRate = ext.sharedResources->swapChainRate;
            }

            break;
        case UserConfiguration::RefreshRate::Original:
        default:
            workloadConfig.targetRate = 0;
            break;
        }

        // Store the rate that was chosen for the configuration.
        ext.sharedResources->targetRate = workloadConfig.targetRate;

#   if RT_ENABLED
        workloadConfig.raytracingEnabled = rtEnabled;

        if (workloadConfig.raytracingEnabled && (ext.sharedResources->rtConfigChanged || ext.sharedResources->fbConfigChanged || sizeChanged)) {
            // Only load the RT pipeline if the device supports it.
             if (ext.device->getCapabilities().raytracing && !ext.rtShaderCache->isSetup()) {
                 ext.rtShaderCache->setup();
            }

            framebufferRenderer->setRaytracingConfig(ext.sharedResources->rtConfig, ext.sharedResources->fbConfigChanged || sizeChanged);
            ext.sharedResources->rtConfigChanged = false;
        }
#   endif
        
        workloadConfig.postBlendNoise = ext.sharedResources->emulatorConfig.dither.postBlendNoise;
        workloadConfig.postBlendNoiseNegative = ext.sharedResources->emulatorConfig.dither.postBlendNoiseNegative;
        
        if (ext.sharedResources->fbConfigChanged || sizeChanged) {
            {
                // Wait until the other queue has stopped using the interpolated color targets.
                std::unique_lock<std::mutex> interpolatedLock(ext.sharedResources->interpolatedMutex);
                InterpolatedFrameCounters &curFrameCounters = ext.sharedResources->interpolatedFrames[ext.sharedResources->interpolatedFramesIndex];
                ext.sharedResources->interpolatedCondition.wait(interpolatedLock, [&]() {
                    return curFrameCounters.presented >= curFrameCounters.available;
                });
            }

            std::scoped_lock<std::mutex> managerLock(ext.sharedResources->managerMutex);
            FramebufferManager &fbManager = ext.sharedResources->framebufferManager;
            RenderTargetManager &targetManager = ext.sharedResources->renderTargetManager;
            renderFramebufferManager->destroyAll();
            targetManager.destroyAll();
            fbManager.destroyAllTileCopies();
            ext.sharedResources->fbConfigChanged = false;
            ext.sharedResources->interpolatedColorTargets.clear();
        }

        if (ext.sharedResources->userConfigChanged) {
            idleMutex.lock();
            idleActive = ext.sharedResources->userConfig.idleWorkActive;
            idleMutex.unlock();
            idleCondition.notify_all();
        }
    }

    void WorkloadQueue::threadConfigurationValidate() {
        const std::scoped_lock lock(ext.sharedResources->configurationMutex);
        if (ext.sharedResources->userConfigChanged) {
            ext.sharedResources->newConfigValidated = true;
            ext.sharedResources->userConfigChanged = false;
        }
    }
    
    // Pokemon Snap port: overwrites a render target with the previous frame's
    // presented image. The console never displayed a camera cut's transit
    // frame -- the cut frame is the heaviest of the scene, the RCP overran,
    // and gtl skipped the draw, holding the previous image for a tick -- so
    // the port shows the same thing by copying the previous frame's image
    // over everything the transit interval presents. The frame itself still
    // renders first: the game reads its framebuffer back, so its state stays
    // exactly what the game computed; only the screen holds. The copy is only
    // an identity when the sizes agree; otherwise the hold is skipped and the
    // frame shows as rendered, the port's pre-hold behavior.
    bool WorkloadQueue::threadHoldCopy(RenderTarget *srcTarget, const RenderTargetKey &srcKey, RenderTarget *dstTarget, const RenderTargetKey &dstKey) {
        std::scoped_lock<std::mutex> managerLock(ext.sharedResources->workloadMutex);
        RenderTargetManager &targetManager = ext.sharedResources->renderTargetManager;
        RenderTarget *srcPtr = (srcTarget != nullptr) ? srcTarget : &targetManager.get(srcKey);
        if (dstTarget == nullptr) {
            if (dstKey.isEmpty()) {
                return false;
            }
            dstTarget = &targetManager.get(dstKey);
        }
        RenderTarget &src = *srcPtr;
        if (src.isEmpty() || (&src == dstTarget)) {
            return false;
        }

        const uint32_t grownWidth = std::max(dstTarget->width, src.width);
        const uint32_t grownHeight = std::max(dstTarget->height, src.height);
        if ((grownWidth != src.width) || (grownHeight != src.height)) {
            return false;
        }

        workerMutex.lock();
        RenderWorker *worker = ext.workloadGraphicsWorker;
        worker->commandList->begin();
        dstTarget->resize(worker, src.width, src.height);

        RenderTextureBarrier copyBarriers[] = {
            RenderTextureBarrier(src.texture.get(), RenderTextureLayout::COPY_SOURCE),
            RenderTextureBarrier(dstTarget->texture.get(), RenderTextureLayout::COPY_DEST),
        };
        worker->commandList->barriers(RenderBarrierStage::COPY, copyBarriers, uint32_t(std::size(copyBarriers)));

        const RenderBox srcBox(0, 0, int32_t(src.width), int32_t(src.height));
        worker->commandList->copyTextureRegion(
            RenderTextureCopyLocation::Subresource(dstTarget->texture.get()),
            RenderTextureCopyLocation::Subresource(src.texture.get()),
            0, 0, 0, &srcBox);
        worker->commandList->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(dstTarget->texture.get(), RenderTextureLayout::SHADER_READ));
        worker->commandList->end();
        worker->execute();
        worker->wait();
        workerMutex.unlock();

        // The present queue reads these from the target it shows.
        dstTarget->resolutionScale = src.resolutionScale;
        dstTarget->downsampleMultiplier = src.downsampleMultiplier;
        dstTarget->misalignX = src.misalignX;
        dstTarget->invMisalignX = src.invMisalignX;
        return true;
    }

    void WorkloadQueue::threadRenderFrame(GameFrame &curFrame, const GameFrame &prevFrame, const WorkloadConfiguration &workloadConfig,
        const DebuggerRenderer &debuggerRenderer, const DebuggerCamera &debuggerCamera, float curFrameWeight, float prevFrameWeight,
        float deltaTimeMs, RenderTargetKey overrideTargetKey, int32_t overrideTargetFbPairIndex, RenderTarget *overrideTarget,
        uint32_t overrideTargetModifier, bool uploadVelocity, bool uploadExtras, bool interpolateTiles, bool interpolateLookAts,
        bool interpolationSubFrame)
    {
#   if ENABLE_HIGH_RESOLUTION_RENDERER
        std::scoped_lock<std::mutex> managerLock(ext.sharedResources->workloadMutex);
        FramebufferManager &fbManager = ext.sharedResources->framebufferManager;
        RenderTargetManager &targetManager = ext.sharedResources->renderTargetManager;
        const bool usingMSAA = (targetManager.multisampling.sampleCount > 1);

        rendererCPUProfiler.start();

        const bool aspectRatioAdjustment = (abs(workloadConfig.aspectRatioScale - 1.0f) > 1e-6f);
        const bool processProjections = aspectRatioAdjustment || prevFrame.matched|| curFrame.isDebuggerCameraEnabled(*this);
        bool uploadProjections = false;
        if (processProjections) {
            ProjectionProcessor::ProcessParams projParams;
            projParams.worker = ext.workloadGraphicsWorker;
            projParams.workloadQueue = this;
            projParams.curFrame = &curFrame;
            projParams.prevFrame = &prevFrame;
            projParams.curFrameWeight = curFrameWeight;
            projParams.prevFrameWeight = prevFrameWeight;
            projParams.aspectRatioScale = workloadConfig.aspectRatioScale;
            projectionProcessor.process(projParams);
            projectionProcessor.upload(projParams);
            uploadProjections = true;
        }

        // Pokemon Snap port, diagnostic: one line of the frame's vital signs
        // per rendered display frame -- matching state, pairing counts, how
        // much was drawn. An anomalous frame identifies itself as the line
        // that differs. Behind the diagnostics gate along with everything
        // that exists only to feed it: the walk over the frame map is not
        // free, and neither is stdout in a render loop.
        if (snapdiag::diagEnabled() || snapdiag::captureEnabled()) {
            static uint32_t vitalFrame = 0;
            vitalFrame++;
            uint32_t transformCount = 0, mappedCount = 0, fbPairs = 0, calls = 0;
            for (uint32_t w : curFrame.workloads) {
                const Workload &wl = workloads[w];
                transformCount += uint32_t(wl.drawData.worldTransforms.size());
                fbPairs += wl.fbPairCount;
                calls += wl.gameCallCount;
                const GameFrameMap::WorkloadMap &wm = curFrame.frameMap.workloads[w];
                if (wm.mapped) {
                    for (const auto &tm : wm.transforms) {
                        if (tm.mapped) {
                            mappedCount++;
                        }
                    }
                }
            }
            if (snapdiag::diagEnabled()) {
                fprintf(stdout, "[SNAP-VITAL] f%u matched %u xf %u paired %u fb %u calls %u\n",
                    vitalFrame, prevFrame.matched ? 1u : 0u, transformCount, mappedCount, fbPairs, calls);
            }

            // Pokemon Snap port: on the frames where most of the scene's
            // transform identities change at once -- the block-transition and
            // spawn frames -- ask the game side to dump the framebuffers
            // around this moment (src/frame_dump.cpp), so the artifact itself
            // lands on disk as images. store() rather than read-modify-write:
            // the game thread decrements this concurrently.
            if (snapdiag::captureEnabled() && prevFrame.matched && (transformCount >= 10) && (mappedCount * 5 < transformCount * 3)) {
                snap_frame_dump_pending.store(6);
            }

            // The framebuffer pair count spikes from four to eight or more on
            // exactly the frames that flash. Whatever those extra passes are,
            // they decide which image the frame presents, so on spike frames
            // each pass prints what it drew and where.
            if (snapdiag::diagEnabled() && (fbPairs > 6)) {
                for (uint32_t w : curFrame.workloads) {
                    const Workload &wl = workloads[w];
                    for (uint32_t f = 0; f < wl.fbPairCount; f++) {
                        const FramebufferPair &fp = wl.fbPairs[f];
                        fprintf(stdout, "[SNAP-FBP] f%u pair %u color %08X w %u siz %u depth %08X zr %u zw %u reason %u projs %u calls %u rect (%d,%d)-(%d,%d)\n",
                            vitalFrame, f, fp.colorImage.address, fp.colorImage.width, fp.colorImage.siz,
                            fp.depthImage.address, fp.depthRead ? 1u : 0u, fp.depthWrite ? 1u : 0u,
                            uint32_t(fp.flushReason), fp.projectionCount,
                            fp.gameCallCount, fp.drawColorRect.ulx, fp.drawColorRect.uly,
                            fp.drawColorRect.lrx, fp.drawColorRect.lry);
                    }
                }
                fflush(stdout);
            }
            if ((vitalFrame % 32) == 0) {
                fflush(stdout);
            }
        }

        const bool processTransforms = prevFrame.matched;
        bool uploadTransforms = false;
        if (processTransforms) {
            TransformProcessor::ProcessParams transformParams;
            transformParams.worker = ext.workloadGraphicsWorker;
            transformParams.workloadQueue = this;
            transformParams.curFrame = &curFrame;
            transformParams.prevFrame = &prevFrame;
            transformParams.curFrameWeight = curFrameWeight;
            transformParams.prevFrameWeight = prevFrameWeight;
            transformProcessor.process(transformParams);
            transformProcessor.upload(transformParams);
            uploadTransforms = true;
        }

        bool uploadTiles = false;
        if (interpolateTiles) {
            TileProcessor::ProcessParams tileParams;
            tileParams.worker = ext.workloadGraphicsWorker;
            tileParams.workloadQueue = this;
            tileParams.curFrame = &curFrame;
            tileParams.prevFrame = &prevFrame;
            tileParams.curFrameWeight = curFrameWeight;
            tileParams.prevFrameWeight = prevFrameWeight;
            tileProcessor.process(tileParams);
            tileProcessor.upload(tileParams);
            uploadTiles = true;
        }

        bool uploadLookAts = false;
        if (interpolateLookAts) {
            LookAtProcessor::ProcessParams lookAtParams;
            lookAtParams.worker = ext.workloadGraphicsWorker;
            lookAtParams.workloadQueue = this;
            lookAtParams.curFrame = &curFrame;
            lookAtParams.prevFrame = &prevFrame;
            lookAtParams.curFrameWeight = curFrameWeight;
            lookAtParams.prevFrameWeight = prevFrameWeight;
            lookAtProcessor.process(lookAtParams);
            lookAtProcessor.upload(lookAtParams);
            uploadLookAts = true;
        }

        // Reset the max height tracking for all active framebuffers.
        fbManager.resetTracking();

        if ((overrideTarget != nullptr) && !usingMSAA) {
            targetManager.setOverride(overrideTargetKey, overrideTarget);
        }

        for (uint32_t w = 0; w < curFrame.workloads.size(); w++) {
            Workload &workload = workloads[curFrame.workloads[w]];

            // There's no guarantee the RSP was processed if framebuffers were not rendered.
            const bool processRSP = true;
            if (processRSP) {
                workload.resetRSPOutputBuffers();

                RSPProcessor::ProcessParams rspParams;
                rspParams.worker = ext.workloadGraphicsWorker;
                rspParams.drawData = &workload.drawData;
                rspParams.drawBuffers = &workload.drawBuffers;
                rspParams.outputBuffers = &workload.outputBuffers;
                rspParams.prevFrameWeight = prevFrameWeight;
                rspParams.curFrameWeight = curFrameWeight;
                rspProcessor->process(rspParams);
            }

            const bool processWorldVertices = prevFrame.matched;
            if (processWorldVertices) {
                workload.resetWorldOutputBuffers();

                VertexProcessor::ProcessParams vertexParams;
                vertexParams.worker = ext.workloadGraphicsWorker;
                vertexParams.drawData = &workload.drawData;
                vertexParams.drawBuffers = &workload.drawBuffers;
                vertexParams.outputBuffers = &workload.outputBuffers;
                vertexParams.curFrameWeight = curFrameWeight;
                vertexParams.prevFrameWeight = prevFrameWeight;
                vertexProcessor->process(vertexParams);
            }

            hlslpp::float2 fixedResScale;
            Framebuffer *colorFb;
            Framebuffer *depthFb;
            uint32_t nativeColorWidth;
            uint32_t nativeColorHeight;
            uint32_t targetWidth;
            uint32_t targetHeight;
            uint32_t targetMisalignX;
            uint32_t rtWidth;
            uint32_t rtHeight;
            RenderTarget *colorTarget;
            RenderTarget *depthTarget;
            RenderFramebufferKey fbKey;
            auto getTargetsFromPair = [&](uint32_t f) {
                const FramebufferPair &fbPair = workload.fbPairs[f];
                const auto &colorImg = fbPair.colorImage;
                const auto &depthImg = fbPair.depthImage;
                fixedResScale = workloadConfig.resolutionScale;
                if (!fbPair.drawColorRect.isEmpty()) {
                    colorFb = nullptr;
                    depthFb = nullptr;
                    nativeColorWidth = colorImg.width;
                    nativeColorHeight = fbPair.drawColorRect.bottom(true);

                    // The height cannot be allowed to shrink to the geometry, because
                    // that height ends up clipping the geometry.
                    //
                    // drawColorRect is a bounding box fitted to where this pass's
                    // vertices landed, and it is fitted at display list parse time,
                    // from the frame's own matrices (rt64_rsp.cpp, posScreen). The
                    // height taken from it becomes the render target's size, reaches
                    // the rasterizer as FbParams.resolution.y, and RasterVS builds
                    // clip space from that -- so the bottom of the fitted box is a
                    // hard clip plane. The vertices that get tested against it are
                    // not the ones it was fitted to: they are positioned by the
                    // interpolated view, one subframe behind. Anything the
                    // interpolation places below where the geometry sat this frame is
                    // clipped away.
                    //
                    // Only the bottom edge does this. The width is colorImg.width,
                    // which the game declared, and the viewport starts at y = 0, so
                    // neither tracks content. That asymmetry is why the artifact this
                    // fixes had one too: pitching the camera down sweeps the world up
                    // the screen, so an interpolated subframe sits lower than the
                    // frame the box was fitted to and crosses the bottom edge, while
                    // pitching up moves geometry towards a fixed y = 0 it was already
                    // inside and never clips anything. The band lost is the residual
                    // screen displacement, a fixed number of pixels, which is most of
                    // a distant model and only the feet of a near one.
                    //
                    // The scissor is the right bound to hold it to. The game authors
                    // it, the real RDP clips against it, and it does not move when the
                    // view is interpolated. Fitting to geometry stays fine for what it
                    // is good for -- deciding how much to allocate and write back --
                    // but it must not be what decides which pixels exist.
                    if (!fbPair.scissorRect.isNull()) {
                        nativeColorHeight = std::max(nativeColorHeight, uint32_t(std::max(0, fbPair.scissorRect.bottom(true))));
                    }

                    // When the target is much bigger than the reference height, we reduce the resolution scaling (but clamped to 1.0).
                    const uint32_t heightThreshold = (workload.viFbSize[1] > 0) ? ((workload.viFbSize[1] * 3) / 2) : 360;
                    uint32_t downsampleMultiplier = workloadConfig.downsampleMultiplier;
                    if ((nativeColorHeight >= heightThreshold) && (fixedResScale[1] >= 2.0f)) {
                        fixedResScale = hlslpp::max(fixedResScale / 2.0f, hlslpp::float2(1.0f, 1.0f));
                        downsampleMultiplier = std::max(downsampleMultiplier / 2U, 1U);
                    }

                    if (fbPair.depthRead || fbPair.depthWrite || fbPair.fastPaths.clearDepthOnly) {
                        uint32_t depthAddress = fbPair.fastPaths.clearDepthOnly ? colorImg.address : depthImg.address;
                        depthFb = &fbManager.get(depthAddress, G_IM_SIZ_16b, nativeColorWidth, nativeColorHeight);
                        depthFb->everUsedAsDepth = true;
                    }
                    else {
                        depthFb = nullptr;
                    }

                    // Ensure dimensions are the same for the color and depth targets based on their previous sizes.
                    fbKey = RenderFramebufferKey();

                    if (!fbPair.fastPaths.clearDepthOnly) {
                        colorFb = &fbManager.get(colorImg.address, colorImg.siz, nativeColorWidth, nativeColorHeight);
                    }

                    if (colorFb != nullptr) {
                        fbKey.colorTargetKey = RenderTargetKey(colorFb->addressStart, colorFb->width, colorFb->siz, Framebuffer::Type::Color);
                        colorTarget = &targetManager.get(fbKey.colorTargetKey);
                    }
                    else {
                        colorTarget = nullptr;
                    }

                    // Apply the modifier key if we retrieved the override target.
                    if ((colorTarget != nullptr) && (colorTarget == overrideTarget)) {
                        fbKey.modifierKey = overrideTargetModifier;
                    }

                    fixedResScale = RenderTarget::computeFixedResolutionScale(colorImg.width, fixedResScale);
                    RenderTarget::computeScaledSize(nativeColorWidth, nativeColorHeight, fixedResScale, targetWidth, targetHeight, targetMisalignX);

                    rtWidth = targetWidth;
                    rtHeight = targetHeight;

                    // The desired size should not be less than the existing size of the color and depth targets.
                    RenderTarget *chosenRt = nullptr;
                    if (depthFb != nullptr) {
                        fbKey.depthTargetKey = RenderTargetKey(depthFb->addressStart, depthFb->width, depthFb->siz, Framebuffer::Type::Depth);
                        depthTarget = &targetManager.get(fbKey.depthTargetKey);
                        depthTarget->resolutionScale = fixedResScale;
                        rtWidth = std::max(rtWidth, depthTarget->width);
                        rtHeight = std::max(rtHeight, depthTarget->height);
                        chosenRt = depthTarget;
                    }
                    else {
                        depthTarget = nullptr;
                    }

                    if (colorTarget != nullptr) {
                        rtWidth = std::max(rtWidth, colorTarget->width);
                        rtHeight = std::max(rtHeight, colorTarget->height);
                        chosenRt = colorTarget;
                    }

                    assert(chosenRt != nullptr);
                    chosenRt->resolutionScale = fixedResScale;
                    chosenRt->downsampleMultiplier = downsampleMultiplier;
                    chosenRt->misalignX = targetMisalignX;
                    chosenRt->invMisalignX = (targetMisalignX > 0) ? (std::lround(fixedResScale.y) - targetMisalignX) : 0;

                    assert((colorTarget != nullptr) || (depthTarget != nullptr));
                    return true;
                }
                else {
                    return false;
                }
            };

            thread_local std::unordered_set<RenderTarget *> resizedTargets;
            thread_local std::vector<std::pair<RenderTarget *, RenderTarget *>> colorDepthPairs;
            resizedTargets.clear();
            colorDepthPairs.clear();

            const uint32_t fbPairCount = (debuggerRenderer.framebufferIndex >= 0) ? (debuggerRenderer.framebufferIndex + 1) : workload.fbPairCount;
            for (uint32_t f = 0; f < fbPairCount; f++) {
                const FramebufferPair &fbPair = workload.fbPairs[f];
#           if RT_ENABLED
                for (uint32_t p = 0; p < fbPair.projectionCount; p++) {
                    const Projection &proj = fbPair.projections[p];
                    const bool perspProj = (proj.type == Projection::Type::Perspective);
                    const bool rtProj = (perspProj && workloadConfig.raytracingEnabled && fbPair.depthWrite); // TODO: Move this condition out of here, ideally by moving the shader submission elsewhere.
                    if (!rtProj) {
                        continue;
                    }

                    // Submit RT shaders if it's an RT proj.
                    for (uint32_t d = 0; d < proj.gameCallCount; d++) {
                        const GameCall &call = proj.gameCalls[d];
                        ext.rtShaderCache->submit(call.shaderDesc);
                    }
                }
#           endif

                // Resize the render targets for this framebuffer pair if necessary.
                if (getTargetsFromPair(f)) {
                    // Resize the native target buffers.
                    if (colorFb != nullptr) {
                        colorFb->nativeTarget.resetBufferHistory();
                    }

                    if (depthFb != nullptr) {
                        depthFb->nativeTarget.resetBufferHistory();
                    }

                    if ((colorTarget != nullptr) && colorTarget->resize(ext.workloadGraphicsWorker, rtWidth, rtHeight)) {
                        resizedTargets.emplace(colorTarget);
                        colorFb->readHeight = 0;
                    }

                    // Set up the dummy target used for rendering the depth if no depth framebuffer is active.
                    if (depthFb == nullptr) {
                        if (dummyDepthTarget == nullptr) {
                            dummyDepthTarget = std::make_unique<RenderTarget>(0, Framebuffer::Type::Depth, targetManager.multisampling, targetManager.usesHDR);
                            dummyDepthTarget->setupDepth(ext.workloadGraphicsWorker, rtWidth, rtHeight);
                        }

                        if ((dummyDepthTarget != nullptr) && dummyDepthTarget->resize(ext.workloadGraphicsWorker, rtWidth, rtHeight)) {
                            resizedTargets.emplace(dummyDepthTarget.get());
                        }

                        if (colorTarget != nullptr) {
                            colorDepthPairs.emplace_back(colorTarget, dummyDepthTarget.get());
                        }
                    }
                    else if (depthTarget != nullptr) {
                        if (colorTarget != nullptr) {
                            colorDepthPairs.emplace_back(colorTarget, depthTarget);
                        }

                        if (depthTarget->resize(ext.workloadGraphicsWorker, rtWidth, rtHeight)) {
                            resizedTargets.emplace(depthTarget);
                            depthFb->readHeight = 0;
                        }
                    }
                }

                fbManager.setupOperations(ext.workloadGraphicsWorker, fbPair.startFbOperations, fixedResScale, targetManager, &resizedTargets);
                fbManager.setupOperations(ext.workloadGraphicsWorker, fbPair.endFbOperations, fixedResScale, targetManager, &resizedTargets);
            }

            // Make sure all depth targets are at least bigger than their corresponding color targets.
            for (auto colorDepthPair : colorDepthPairs) {
                if (colorDepthPair.second->resize(ext.workloadGraphicsWorker, colorDepthPair.first->width, colorDepthPair.first->height)) {
                    resizedTargets.emplace(colorDepthPair.second);
                }
            }

            for (RenderTarget *renderTarget : resizedTargets) {
                renderFramebufferManager->destroyAllWithRenderTarget(renderTarget);
            }

            uint32_t gameCallCursor = 0;
            const uint32_t gameCallCountMax = (debuggerRenderer.globalDrawCallIndex >= 0) ? (debuggerRenderer.globalDrawCallIndex + 1) : workload.gameCallCount;
            thread_local std::vector<BufferUploader *> bufferUploaders;
            bufferUploaders.clear();

            // Indicate to the texture cache the textures must not be deleted.
            ext.textureCache->incrementLock();

            // Reset the texture cache vectors for the framebuffer renderer.
            framebufferRenderer->updateTextureCache(ext.textureCache);

            for (uint32_t f = 0; f < fbPairCount; f++) {
                const FramebufferPair &fbPair = workload.fbPairs[f];
                fbManager.performDiscards(fbPair.startFbDiscards);
            }
            
            // Add all framebuffer pairs to the framebuffer renderer and setup the operations.
            scratchFbChangePool.reset();
            fbManager.resetOperations();
            framebufferRenderer->resetFramebuffers(ext.workloadGraphicsWorker, ubershadersVisible, workload.extended.ditherNoiseStrength, targetManager.multisampling);

#       if RT_ENABLED
            if (workloadConfig.raytracingEnabled) {
                framebufferRenderer->resetRaytracing(ext.rtShaderCache, ext.blueNoiseTexture);
            }
#       endif

            for (uint32_t f = 0; f < fbPairCount; f++) {
                const FramebufferPair &fbPair = workload.fbPairs[f];
                if (getTargetsFromPair(f)) {
                    RenderFramebufferStorage &fbStorage = renderFramebufferManager->get(fbKey, colorTarget, (depthTarget != nullptr) ? depthTarget : dummyDepthTarget.get());
                    FramebufferRenderer::DrawParams drawParams;
                    drawParams.worker = ext.workloadGraphicsWorker;
                    drawParams.fbStorage = &fbStorage;
                    drawParams.curWorkload = &workload;
                    drawParams.fbPairIndex = f;
                    drawParams.fbWidth = nativeColorWidth;
                    drawParams.fbHeight = nativeColorHeight;
                    drawParams.targetWidth = targetWidth;
                    drawParams.targetHeight = targetHeight;
                    drawParams.rasterShaderCache = ext.rasterShaderCache;
                    drawParams.resolutionScale = fixedResScale;
                    drawParams.aspectRatioSource = workloadConfig.aspectRatioSource;
                    drawParams.aspectRatioTarget = workloadConfig.aspectRatioTarget;
                    drawParams.extAspectPercentage = workloadConfig.extAspectPercentage;
                    drawParams.horizontalMisalignment = (colorTarget != nullptr) ? float(colorTarget->misalignX) : float(depthTarget->misalignX);
                    drawParams.presetScene = curFrame.presetScene;
                    drawParams.rtEnabled = workloadConfig.raytracingEnabled;
                    drawParams.submissionFrame = workload.submissionFrame;
                    drawParams.deltaTimeMs = deltaTimeMs;
                    drawParams.ubershadersOnly = ubershadersOnly;
                    drawParams.postBlendNoise = workloadConfig.postBlendNoise;
                    drawParams.postBlendNoiseNegative = workloadConfig.postBlendNoiseNegative;
                    drawParams.maxGameCall = std::min(gameCallCountMax - gameCallCursor, fbPair.gameCallCount);
                    framebufferRenderer->addFramebuffer(drawParams);
                }
                
                gameCallCursor += fbPair.gameCallCount;
            }

            // Create all GPU tile mappings and upload them.
            if (!workload.drawData.gpuTiles.empty()) {
                std::pair<size_t, size_t> gpuTileRange;
                gpuTileRange.first = 0;
                gpuTileRange.second = workload.drawData.gpuTiles.size();
                framebufferRenderer->createGPUTiles(workload.drawData.callTiles.data(), uint32_t(workload.drawData.gpuTiles.size()),
                    workload.drawData.gpuTiles.data(), &fbManager, ext.textureCache, workload.submissionFrame);

                // Upload the GPU tiles.
                ext.workloadTilesUploader->submit(ext.workloadGraphicsWorker, {
                    { workload.drawData.gpuTiles.data(), gpuTileRange, sizeof(interop::GPUTile), RenderBufferFlag::STORAGE, { }, &workload.drawBuffers.gpuTilesBuffer}
                });

                bufferUploaders.emplace_back(ext.workloadTilesUploader);
            }

            if (uploadVelocity) {
                bufferUploaders.emplace_back(ext.workloadVelocityUploader);
                uploadVelocity = false;
            }

            if (uploadExtras) {
                bufferUploaders.emplace_back(ext.workloadExtrasUploader);
                uploadExtras = false;
            }

            if (uploadProjections) {
                bufferUploaders.emplace_back(projectionProcessor.bufferUploader.get());
                uploadProjections = false;
            }

            if (uploadTransforms) {
                bufferUploaders.emplace_back(transformProcessor.bufferUploader.get());
                uploadTransforms = false;
            }

            if (uploadTiles) {
                bufferUploaders.emplace_back(tileProcessor.bufferUploader.get());
                uploadTiles = false;
            }

            if (uploadLookAts) {
                bufferUploaders.emplace_back(lookAtProcessor.bufferUploader.get());
                uploadLookAts = false;
            }

#       if RT_ENABLED
            if (workloadConfig.raytracingEnabled) {
                ext.rtShaderCache->setNextState();
            }
#       endif

            workerMutex.lock();
            ext.workloadGraphicsWorker->commandList->begin();
            ext.workloadGraphicsWorker->commandList->resetQueryPool(queryPool.get(), 0, 2);
            ext.workloadGraphicsWorker->commandList->writeTimestamp(queryPool.get(), 0);
            framebufferRenderer->endFramebuffers(ext.workloadGraphicsWorker, &workload.drawBuffers, &workload.outputBuffers, workloadConfig.raytracingEnabled);
            framebufferRenderer->recordSetup(ext.workloadGraphicsWorker, bufferUploaders, processRSP ? rspProcessor.get() : nullptr, processWorldVertices ? vertexProcessor.get() : nullptr, &workload.outputBuffers, workloadConfig.raytracingEnabled);
            
            // Record all framebuffer pairs.
            uint32_t framebufferIndex = 0;
            for (uint32_t f = 0; f < fbPairCount; f++) {
                const FramebufferPair &fbPair = workload.fbPairs[f];
                bool validTargets = getTargetsFromPair(f);
                fbManager.recordOperations(ext.workloadGraphicsWorker, &workload.fbChangePool, &workload.fbStorage, ext.shaderLibrary, ext.textureCache,
                    fbPair.startFbOperations, targetManager, fixedResScale, f, workload.submissionFrame);

                if (validTargets) {
                    const auto &colorImg = fbPair.colorImage;
                    const auto &depthImg = fbPair.depthImage;
                    bool colorFormatUpdated = false;
                    if (colorFb != nullptr) {
                        if (colorImg.formatChanged) {
                            colorFb->discardLastWrite();
                        }
                        else if (colorFb->isLastWriteDifferent(Framebuffer::Type::Color)) {
                            RenderTargetKey otherColorTargetKey(colorFb->addressStart, colorFb->width, colorFb->siz, colorFb->lastWriteType);
                            RenderTarget &otherColorTarget = targetManager.get(otherColorTargetKey);
                            if (!otherColorTarget.isEmpty()) {
                                const FixedRect &r = colorFb->lastWriteRect;
                                colorTarget->copyFromTarget(ext.workloadGraphicsWorker, &otherColorTarget, r.left(false), r.top(false), r.width(false, true), r.height(false, true), ext.shaderLibrary);
                                colorFb->discardLastWrite();
                                colorFormatUpdated = true;
                            }
                        }

                        if (colorImg.formatChanged) {
                            colorTarget->clearColorTarget(ext.workloadGraphicsWorker);
                            colorFb->readHeight = 0;
                        }

                        if (colorFb->height > colorFb->readHeight) {
                            uint32_t readRowCount = colorFb->height - colorFb->readHeight;
                            FramebufferChange *colorFbChange = colorFb->readChangeFromStorage(ext.workloadGraphicsWorker, workload.fbStorage, scratchFbChangePool,
                                Framebuffer::Type::Color, colorImg.fmt, f, colorFb->readHeight, readRowCount, ext.shaderLibrary);

                            if (colorFbChange != nullptr) {
                                colorTarget->copyFromChanges(ext.workloadGraphicsWorker, *colorFbChange, colorFb->width, readRowCount, colorFb->readHeight, ext.shaderLibrary);
                            }

                            colorFb->readHeight = colorFb->height;
                        }
                    }

                    bool depthFormatUpdated = false;
                    bool depthFbChanged = false;
                    bool depthFbTypeChanged = false;
                    if (depthFb != nullptr) {
                        depthFbTypeChanged = (depthFb->lastWriteType == Framebuffer::Type::Color);

                        bool imgFormatChanged = (colorFb == nullptr) ? colorImg.formatChanged : depthImg.formatChanged;
                        if (imgFormatChanged) {
                            depthFb->discardLastWrite();
                        }
                        else if (depthFb->isLastWriteDifferent(Framebuffer::Type::Depth)) {
                            RenderTargetKey otherDepthTargetKey(depthFb->addressStart, depthFb->width, depthFb->siz, depthFb->lastWriteType);
                            RenderTarget &otherDepthTarget = targetManager.get(otherDepthTargetKey);
                            if (!otherDepthTarget.isEmpty()) {
                                const FixedRect &r = depthFb->lastWriteRect;
                                depthTarget->copyFromTarget(ext.workloadGraphicsWorker, &otherDepthTarget, r.left(false), r.top(false), r.width(false, true), r.height(false, true), ext.shaderLibrary);
                                depthFb->discardLastWrite();
                                depthFormatUpdated = true;
                            }
                        }

                        // Pokemon Snap port: the clear and reload below keep the
                        // depth target coherent with RDRAM, and they belong to the
                        // raw render only. An interpolated sub-frame is a replay:
                        // its depth continuity is its own passes, rendered at the
                        // interpolated pose. On the frames where the game's 8x8
                        // detector passes interleave with the scene, the shared
                        // depth address flips width every boundary, readHeight
                        // resets each time, and this reload would stomp the
                        // sub-frame's depth with the raw render's final-pose
                        // snapshot -- geometry then z-tests against a ghost of
                        // where the world will be, which is the one-frame flash
                        // at every block transition and spawn corner.
                        if (imgFormatChanged && !interpolationSubFrame) {
                            depthTarget->clearDepthTarget(ext.workloadGraphicsWorker);
                            depthFb->readHeight = 0;
                        }

                        // The watermark only advances when the reload actually
                        // ran. A skipped reload that still marked the rows
                        // consumed would leave the depth target permanently
                        // unseeded: once readHeight equals height nothing ever
                        // re-arms it, not even a later non-interpolated frame.
                        if ((depthFb->height > depthFb->readHeight) && !interpolationSubFrame) {
                            uint32_t readRowCount = depthFb->height - depthFb->readHeight;
                            FramebufferChange *depthFbChange = depthFb->readChangeFromStorage(ext.workloadGraphicsWorker, workload.fbStorage, scratchFbChangePool, Framebuffer::Type::Depth,
                                G_IM_FMT_DEPTH, f, depthFb->readHeight, readRowCount, ext.shaderLibrary);

                            if (depthFbChange != nullptr) {
                                depthTarget->copyFromChanges(ext.workloadGraphicsWorker, *depthFbChange, depthFb->width, readRowCount, depthFb->readHeight, ext.shaderLibrary);
                                depthFbChanged = true;
                            }

                            depthFb->readHeight = depthFb->height;
                        }
                    }
                    
                    framebufferRenderer->recordFramebuffer(ext.workloadGraphicsWorker, framebufferIndex++);

                    // Transition the render targets in case the present queue will show them so it doesn't have to perform transitions.
                    if (colorTarget != nullptr && depthTarget != nullptr) {
                        RenderTextureBarrier textureBarriers[] = {
                            RenderTextureBarrier(colorTarget->texture.get(), RenderTextureLayout::SHADER_READ),
                            RenderTextureBarrier(depthTarget->texture.get(), RenderTextureLayout::SHADER_READ)
                        };

                        ext.workloadGraphicsWorker->commandList->barriers(RenderBarrierStage::GRAPHICS, textureBarriers, uint32_t(std::size(textureBarriers)));
                    }
                    else {
                        RenderTarget *chosenTarget = (colorTarget != nullptr) ? colorTarget : depthTarget;
                        ext.workloadGraphicsWorker->commandList->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(chosenTarget->texture.get(), RenderTextureLayout::SHADER_READ));
                    }

                    // Do the resolve if using MSAA while target override is active and we're on the correct framebuffer pair index.
                    if (usingMSAA && (overrideTarget != nullptr) && ((uint32_t)overrideTargetFbPairIndex == f)) {
                        overrideTarget->resize(ext.workloadGraphicsWorker, colorTarget->width, colorTarget->height);
                        overrideTarget->resolveFromTarget(ext.workloadGraphicsWorker, colorTarget, ext.shaderLibrary);
                    }

                    const uint64_t writeTimestamp = fbManager.nextWriteTimestamp();
                    FixedRect depthFbRect;
                    if (colorFb != nullptr) {
                        colorFb->lastWriteRect.merge(fbPair.drawColorRect.scaled(fixedResScale.x, fixedResScale.y));
                        colorFb->lastWriteType = Framebuffer::Type::Color;
                        colorFb->lastWriteFmt = colorImg.fmt;
                        colorFb->lastWriteTimestamp = writeTimestamp;
                        depthFbRect = fbPair.drawDepthRect;
                    }
                    else {
                        depthFbRect = fbPair.drawColorRect;
                    }
                    
                    const bool depthWrite = ((colorFb == nullptr) || depthFbChanged || depthFbTypeChanged || fbPair.depthWrite) && (depthFb != nullptr);
                    if (depthWrite && !depthFbRect.isNull()) {
                        depthFb->lastWriteRect.merge(depthFbRect.scaled(fixedResScale.x, fixedResScale.y));
                        depthFb->lastWriteType = Framebuffer::Type::Depth;
                        depthFb->lastWriteFmt = G_IM_FMT_DEPTH;
                        depthFb->lastWriteTimestamp = writeTimestamp;
                    }
                }
                
                fbManager.recordOperations(ext.workloadGraphicsWorker, &workload.fbChangePool, &workload.fbStorage, ext.shaderLibrary, ext.textureCache,
                    fbPair.endFbOperations, targetManager, fixedResScale, f, workload.submissionFrame);
            }

            ext.workloadGraphicsWorker->commandList->writeTimestamp(queryPool.get(), 1);
            ext.workloadGraphicsWorker->commandList->end();
            framebufferRenderer->waitForUploaders();
            ext.workloadGraphicsWorker->execute();
            ext.workloadGraphicsWorker->wait();
            workerMutex.unlock();

            // Update the GPU profiler with the results from the timestamps of the frame.
            queryPool->queryResults();
            const uint64_t *frameTimestamps = queryPool->getResults();
            rendererGPUProfiler.log(double(frameTimestamps[1] - frameTimestamps[0]) / 1000000.0);

            // Indicate to the texture cache it's safe to delete the textures if no locks are active.
            ext.textureCache->decrementLock();
        }

        if ((overrideTarget != nullptr) && !usingMSAA) {
            targetManager.removeOverride(overrideTargetKey);
        }

        framebufferRenderer->advanceFrame(workloadConfig.raytracingEnabled);
        rendererCPUProfiler.end();
        rendererCPUProfiler.log();
        rendererCPUProfiler.reset();
#   endif
    }

    void WorkloadQueue::threadAdvanceBarrier() {
        std::scoped_lock<std::mutex> cursorLock(cursorMutex);
        barrierCursor = (barrierCursor + 1) % workloads.size();
    }

    void WorkloadQueue::threadAdvanceWorkloadId(uint64_t newWorkloadId) {
        {
            std::scoped_lock<std::mutex> cursorLock(workloadIdMutex);
            workloadId = newWorkloadId;
        }

        workloadIdCondition.notify_all();
    }

    void WorkloadQueue::renderThreadLoop() {
        Thread::setCurrentThreadName("RT64 Workload");

        WorkloadConfiguration workloadConfig;
        int64_t logicalTicks = 0;
        int64_t displayTicks = 0;
        uint32_t originalRateForTicks = 0;
        uint32_t displayRateForTicks = 0;
        int processCursor = -1;
        bool frameReduction = false;
        // The previous game frame's presented target, for the cut-transit hold.
        // The game's cuts are hard cuts and release as hard cuts; a crossfade
        // tail was tried here and read as a glitch on screen.
        RenderTargetKey snapPrevTargetKey;
        uint32_t snapConsecutiveHolds = 0;
        // How many interpolated targets the last shown frame filled; its final
        // one is the last picture the screen actually displayed.
        uint32_t snapPrevInterpolatedIndex = 0;
        while (threadsRunning) {
            {
                std::unique_lock<std::mutex> cursorLock(cursorMutex);
                cursorCondition.wait(cursorLock, [&]() {
                    return (writeCursor != threadCursor) || !threadsRunning;
                });

                if (threadsRunning) {
                    processCursor = threadCursor;
                    threadCursor = (threadCursor + 1) % workloads.size();
                }
            }

            if (processCursor >= 0) {
                std::unique_lock<std::mutex> threadLock(threadMutex);
                Workload &workload = workloads[processCursor];
                ext.presentQueue->waitForPresentId(workload.presentId);

                if (!threadsRunning) {
                    continue;
                }

                ElapsedTimer workloadTimer;
                workloadProfiler.start();
                threadConfigurationUpdate(workload.viFbSize, workloadConfig);

                // FIXME: This is a very hacky way to find out if we need to advance the frame if the workload was paused for the first time.
                if (!workload.paused || (!gameFrames[curFrameIndex].workloads.empty() && (gameFrames[curFrameIndex].workloads[0] != (uint32_t)processCursor))) {
                    prevFrameIndex = curFrameIndex;
                    curFrameIndex = (curFrameIndex + 1) % gameFrames.size();
                }
                
                // TODO: The frame detection needs to be more elaborate than just matching one workload to one frame.
                GameFrame &curFrame = gameFrames[curFrameIndex];
                const GameFrame &prevFrame = gameFrames[prevFrameIndex];
                uint32_t workloadIndex = processCursor;
                curFrame.set(*this, &workloadIndex, 1);

                // Detect the color image to interpolate for this workload.
                RenderTargetKey interpolationTargetKey;
                int32_t interpolationTargetFbPairIndex = -1;
                {
                    std::scoped_lock<std::mutex> managerLock(ext.sharedResources->managerMutex);
                    FramebufferManager &fbManager = ext.sharedResources->framebufferManager;
                    std::vector<uint32_t> &colorVector = ext.sharedResources->colorImageAddressVector;
                    std::unordered_set<uint32_t> &colorSet = ext.sharedResources->colorImageAddressSet;
                    colorVector.clear();
                    colorSet.clear();
                    for (int32_t f = workload.fbPairCount - 1; f >= 0; f--) {
                        const FramebufferPair &fbPair = workload.fbPairs[f];
                        bool interpolationCandidate = fbPair.earlyPresentCandidate();
                        if (fbPair.drawColorRect.isEmpty()) {
                            continue;
                        }

                        const auto &colorImg = fbPair.colorImage;
                        if (colorSet.find(colorImg.address) != colorSet.end()) {
                            continue;
                        }
                        else {
                            if (interpolationCandidate) {
                                colorVector.push_back(colorImg.address);
                            }

                            colorSet.insert(colorImg.address);
                        }

                        if (!interpolationCandidate || !interpolationTargetKey.isEmpty()) {
                            continue;
                        }

                        Framebuffer *interpolationFb = fbManager.find(colorImg.address);
                        if ((interpolationFb != nullptr) && interpolationFb->interpolationEnabled) {
                            interpolationTargetKey.fbType = Framebuffer::Type::Color;
                            interpolationTargetKey.address = fbPair.colorImage.address;
                            interpolationTargetKey.siz = fbPair.colorImage.siz;
                            interpolationTargetKey.width = fbPair.colorImage.width;
                            interpolationTargetFbPairIndex = f;
                        }
                    }
                }

                float prevFrameWeight = 0.0f;
                float curFrameWeight = 1.0f;
                float deltaTimeMs = 1.0f / 30.0f;
                const bool requiresFrameMatching = (workloadConfig.targetRate > 0) || workloadConfig.raytracingEnabled;
                bool generateInterpolatedFrames = false;
                bool velocityUploaderUsed = false;
                bool tileInterpolationUsed = false;
                bool lookAtInterpolationUsed = false;
                if (requiresFrameMatching) {
                    matchingProfiler.reset();
                    matchingProfiler.start();
                    curFrame.match(ext.workloadGraphicsWorker, *this, prevFrame, ext.workloadVelocityUploader, velocityUploaderUsed, tileInterpolationUsed, lookAtInterpolationUsed);
                    matchingProfiler.end();
                    matchingProfiler.log();

                    const bool displayRateAboveOriginal = (workload.viOriginalRate > 0) && (workloadConfig.targetRate > workload.viOriginalRate);
                    // Cutscenes interpolate fully, like gameplay. Both
                    // partial modes were tried and read wrong: native
                    // cadence stutters on every pan, and view-only (content
                    // pinned to the current frame) runs the whole intro at
                    // half rate. What the film's staged ticks actually need
                    // is to not be shown at all, and that is handled by
                    // verdicts, not weights: the census holds staging off
                    // screen (rt64_game_frame.cpp), camera cuts snap through
                    // their matrix group, and the pose guard keeps shown
                    // pairs from blending across a re-pose.
                    generateInterpolatedFrames = !workload.paused && displayRateAboveOriginal && !interpolationTargetKey.isEmpty();

                    const bool resetTicks = !generateInterpolatedFrames || (originalRateForTicks != workload.viOriginalRate) || (displayRateForTicks != workloadConfig.targetRate) || !displayRateAboveOriginal;
                    if (resetTicks) {
                        logicalTicks = 0;
                        displayTicks = 0;
                        originalRateForTicks = workload.viOriginalRate;
                        displayRateForTicks = workloadConfig.targetRate;
                    }
                }

                // Estimate amount of frames to render based on how many display frames it'd take to reach the next logical frame.
                uint32_t displayFrames = 1;
                if (generateInterpolatedFrames) {
                    logicalTicks += workloadConfig.targetRate;
                    displayFrames = uint32_t((logicalTicks - displayTicks) / workload.viOriginalRate);
                    deltaTimeMs = 1.0f / float(workloadConfig.targetRate);

                    if ((displayFrames > 1) && frameReduction) {
                        displayTicks += workload.viOriginalRate;
                        displayFrames--;
                        frameReduction = false;
                    }

                    assert((logicalTicks > displayTicks) && "Logical ticks must always remain bigger than the display ticks.");
                    assert(((logicalTicks - displayTicks) <= (workloadConfig.targetRate + workload.viOriginalRate)) && "The gap between logical ticks and display ticks can't be bigger than the target rate.");
                    assert((displayFrames > 0) && "At least one display frame must be generated.");
                }
                else if (workload.viOriginalRate > 0) {
                    deltaTimeMs = 1.0f / float(workload.viOriginalRate);
                }


                // Pokemon Snap port: the clock interpolation is built on. Every
                // display frame between two game frames is placed by assuming
                // the game ticks at a fixed rate, so if the ticks themselves
                // arrive unevenly -- or the rate the renderer believes changes
                // underneath it -- the frames still arrive on a perfect
                // schedule while the motion inside them speeds up and slows
                // down. That is stutter no amount of frame counting or present
                // pacing can see, so it is measured here at its source.
                if (snapdiag::statsEnabled()) {
                    static Timestamp lastTickTimestamp;
                    static double tickTotalMs = 0.0;
                    static double tickWorstMs = 0.0;
                    static double tickBestMs = 1e9;
                    static uint32_t tickCount = 0;
                    static uint32_t tickUneven = 0;
                    static uint32_t tickRateChanges = 0;
                    static uint32_t lastOriginalRate = 0;
                    static uint32_t lastDisplayFrames = 0;
                    static uint32_t displayFramesChanges = 0;
                    if (workload.viOriginalRate != lastOriginalRate) {
                        tickRateChanges++;
                        lastOriginalRate = workload.viOriginalRate;
                    }
                    if (displayFrames != lastDisplayFrames) {
                        displayFramesChanges++;
                        lastDisplayFrames = displayFrames;
                    }
                    const Timestamp tickNow = Timer::current();
                    if (lastTickTimestamp != Timestamp()) {
                        const double tickMs = std::chrono::duration<double, std::milli>(tickNow - lastTickTimestamp).count();
                        tickTotalMs += tickMs;
                        tickWorstMs = std::max(tickWorstMs, tickMs);
                        tickBestMs = std::min(tickBestMs, tickMs);
                        tickCount++;
                        const double tickAverageMs = tickTotalMs / tickCount;
                        if (std::abs(tickMs - tickAverageMs) > (tickAverageMs * 0.25)) {
                            tickUneven++;
                        }
                        if (tickCount >= 120) {
                            fprintf(stdout, "[SNAP-TICK] %u game frames, average %.2f ms (%.1f fps), fastest %.2f, slowest %.2f, uneven %u, believed rate %u (changed %u), frames per tick %u (changed %u)\n",
                                tickCount, tickAverageMs, 1000.0 / tickAverageMs, tickBestMs, tickWorstMs, tickUneven,
                                workload.viOriginalRate, tickRateChanges, displayFrames, displayFramesChanges);
                            fflush(stdout);
                            tickTotalMs = 0.0; tickWorstMs = 0.0; tickBestMs = 1e9; tickCount = 0;
                            tickUneven = 0; tickRateChanges = 0; displayFramesChanges = 0;
                        }
                    }
                    lastTickTimestamp = tickNow;
                }

                // Pokemon Snap port: the moment this game frame's content became
                // available to draw. The present side subtracts it to say how
                // old the picture on screen is.
                snapdiag::newestStateNanos().store(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(Timer::current().time_since_epoch()).count(),
                    std::memory_order_relaxed);

                // A tick that yields one image is a whole game frame in which
                // the picture cannot change, however smoothly it is delivered.
                if ((displayFrames <= 1) && requiresFrameMatching && !workload.paused) {
                    snapdiag::singleFrameTickCounter().fetch_add(1, std::memory_order_relaxed);
                    if (snapdiag::statsEnabled()) {
                        fprintf(stdout, "[SNAP-ONEFRAME] tick produced a single image: game rate %u, target rate %u, interpolation target %s, frames %u\n",
                            workload.viOriginalRate, workloadConfig.targetRate,
                            interpolationTargetKey.isEmpty() ? "missing" : "present", displayFrames);
                        fflush(stdout);
                    }
                }

                snapdiag::subFrameAskedCounter().fetch_add(displayFrames, std::memory_order_relaxed);
                ext.sharedResources->viOriginalRate = workload.viOriginalRate;
                
                // Get the current and previous set of frame counters. The other set can be in use by the present queue. Skip if no new present event has arrived before this workload event.
                InterpolatedFrameCounters &prevFrameCounters = ext.sharedResources->interpolatedFrames[ext.sharedResources->interpolatedFramesIndex];
                const bool useDifferentCounters = (lastPresentId != workload.presentId);
                if (useDifferentCounters) {
                    ext.sharedResources->interpolatedFramesIndex = ext.sharedResources->interpolatedFramesIndex ^ 1;
                    lastPresentId = workload.presentId;
                }
                // If the same set of counters is used, we wait until the presentation of its targets is finished so the targets are available to use. Waiting is ignored
                // if the frame counter has never presented anything yet, as it'll only be a valid value if the previous present event actually did something.
                else if (generateInterpolatedFrames) {
                    std::unique_lock<std::mutex> interpolatedLock(ext.sharedResources->interpolatedMutex);
                    ext.sharedResources->interpolatedCondition.wait(interpolatedLock, [&]() {
                        return (prevFrameCounters.presented == 0) || (prevFrameCounters.presented >= prevFrameCounters.available);
                    });
                }

                InterpolatedFrameCounters &curFrameCounters = ext.sharedResources->interpolatedFrames[ext.sharedResources->interpolatedFramesIndex];
                curFrameCounters.skipped = false;
                curFrameCounters.presented = 0;
                curFrameCounters.available = 0;
                curFrameCounters.count = displayFrames;

                // Create as many render targets as required to store the interpolated targets.
                auto &interpolatedTargets = ext.sharedResources->interpolatedColorTargets;
                const bool usingMSAA = (ext.sharedResources->renderTargetManager.multisampling.sampleCount > 1);
                const bool usesHDR = ext.sharedResources->renderTargetManager.usesHDR;
                uint32_t requiredFrames = (usingMSAA && generateInterpolatedFrames) ? displayFrames : (displayFrames - 1);
                if ((requiredFrames > 0) && (interpolatedTargets.size() < requiredFrames)) {
                    // Pokemon Snap port: the present thread indexes this same
                    // vector while showing the frames of the tick before this
                    // one. Growing it here without the lock can move every
                    // element while that is happening, which hands the screen
                    // a pointer into freed memory -- rare, silent, and
                    // catastrophic when it lands. The number of frames a tick
                    // needs changes whenever the display or the game's rate
                    // does, so this is not a startup-only path. The targets
                    // themselves allocate nothing here; they are sized later,
                    // so the lock is held only for the bookkeeping.
                    std::scoped_lock<std::mutex> interpolatedLock(ext.sharedResources->interpolatedMutex);
                    uint32_t previousSize = uint32_t(interpolatedTargets.size());
                    interpolatedTargets.resize(requiredFrames);
                    for (uint32_t i = previousSize; i < requiredFrames; i++) {
                        interpolatedTargets[i] = std::make_unique<RenderTarget>(interpolationTargetKey.address, Framebuffer::Type::Color, RenderMultisampling(), usesHDR);
                    }
                }
                
                // The console never displayed a cut's transit frame: its draw
                // overran and gtl held the previous image for a tick. Matched
                // here by presenting the previous frame's image for this
                // workload's whole interval, at native rate and interpolated
                // alike. MSAA resolves through different targets; the hold
                // stands down there rather than guess. The census verdict is
                // only trusted when the frame matcher actually ran this
                // workload -- with matching off it is whatever frame last
                // computed it.
                bool snapCutHold = (workload.snapCutHold || (requiresFrameMatching && curFrame.snapDiscontinuity)) &&
                    !workload.paused && !usingMSAA &&
                    !interpolationTargetKey.isEmpty() && !snapPrevTargetKey.isEmpty();

                // Release valve on the renderer-side verdict: a scene that
                // churns endlessly must not freeze the screen. Sixteen frames
                // outlasts every observed transition; past it the valve stays
                // open for as long as the verdict keeps firing, so endless
                // churn shows continuously rather than freezing in bursts.
                if (snapCutHold && (snapConsecutiveHolds >= 16)) {
                    snapCutHold = false;
                    snapConsecutiveHolds++;
                }
                if (snapCutHold) {
                    // Snapshot the previous image before this frame renders a
                    // single pass; the transit workload's scene-init clears
                    // can reach into the previous frame's buffer, and a hold
                    // copied after rendering presents that half-wiped image
                    // -- the camera prop vanishing for a frame at the cut
                    // out of the intro's close-up shot.
                    {
                        std::scoped_lock<std::mutex> managerLock(ext.sharedResources->workloadMutex);
                        RenderTargetManager &targetManager = ext.sharedResources->renderTargetManager;
                        const RenderTarget &holdSrc = targetManager.get(snapPrevTargetKey);
                        const bool scratchMismatch = (snapHoldScratch != nullptr) && !snapHoldScratch->isEmpty() &&
                            ((snapHoldScratch->width != holdSrc.width) || (snapHoldScratch->height != holdSrc.height));
                        if ((snapHoldScratch == nullptr) || scratchMismatch) {
                            snapHoldScratch = std::make_unique<RenderTarget>(snapPrevTargetKey.address, Framebuffer::Type::Color, RenderMultisampling(), usesHDR);
                        }
                    }
                    // Hold the last picture the screen actually showed, not
                    // the first one of the previous game frame. The main
                    // target carries that frame's opening pose; everything
                    // after it was drawn into the interpolated targets, so
                    // holding the main target does not stop the picture -- it
                    // steps it backwards by most of a game frame, keeps it
                    // there for a frame, and then jumps forwards by two. Two
                    // poses of the same motion reach the eye a few
                    // milliseconds apart, which is seen as two of everything
                    // rather than as a pause.
                    RenderTarget *lastShownTarget = ((snapPrevInterpolatedIndex > 0) &&
                        (size_t(snapPrevInterpolatedIndex) <= interpolatedTargets.size())) ?
                        interpolatedTargets[snapPrevInterpolatedIndex - 1].get() : nullptr;
                    if ((lastShownTarget != nullptr) && !lastShownTarget->isEmpty()) {
                        snapCutHold = threadHoldCopy(lastShownTarget, RenderTargetKey(), snapHoldScratch.get(), RenderTargetKey());
                    }
                    else {
                        snapCutHold = threadHoldCopy(nullptr, snapPrevTargetKey, snapHoldScratch.get(), RenderTargetKey());
                    }
                    // A hold that actually shows spends valve budget; a
                    // failed snapshot copy shows the rendered frame instead
                    // and must not.
                    snapConsecutiveHolds = snapCutHold ? (snapConsecutiveHolds + 1) : 0;
                }
                else if (!workload.snapCutHold && !(requiresFrameMatching && curFrame.snapDiscontinuity)) {
                    // Only a frame with no verdict at all closes the valve.
                    snapConsecutiveHolds = 0;
                }
                if (snapCutHold) {
                    snapdiag::holdCounter().fetch_add(1, std::memory_order_relaxed);
                    if (workload.snapCutHold) {
                        snapdiag::holdFromCameraCounter().fetch_add(1, std::memory_order_relaxed);
                    }
                    if (curFrame.snapDiscontinuity) {
                        snapdiag::holdFromCensusCounter().fetch_add(1, std::memory_order_relaxed);
                    }
                }
                if (workload.snapCutscene) {
                    snapdiag::cutsceneTickCounter().fetch_add(1, std::memory_order_relaxed);
                }
                if (snapCutHold && snapdiag::diagEnabled()) {
                    fprintf(stdout, "[SNAP-HOLD] cut transit: presenting previous frame for one tick\n");
                    fflush(stdout);
                }

                const int64_t originalTimeMicro = (workload.viOriginalRate > 0) ? (1000000 / workload.viOriginalRate) : 0;
                const int64_t setupTimeMicro = workloadTimer.elapsedMicroseconds();
                const int64_t adjustedTimeWindowMicro = originalTimeMicro - setupTimeMicro;
                const int64_t maxTimePerFrameMicro = adjustedTimeWindowMicro / displayFrames;
                bool skippedFrames = false;
                bool skipWorkloadNow = false;
                uint32_t targetIndex = 0;
                uint32_t framesRendered = 0;
                int64_t renderTimeTotalMicro = 0;
                for (uint32_t frame = 0; (frame < displayFrames) && !skipWorkloadNow; frame++) {
                    // Evaluate if this frame should be skipped. Measure the current time and compare it to what frame is estimated should be have been rendered by now.
                    if ((frame > 0) && (originalTimeMicro > 0)) {
                        const int64_t currentTimeMicro = workloadTimer.elapsedMicroseconds() - setupTimeMicro;
                        const int64_t expectedTimeMicro = frame * maxTimePerFrameMicro;
                        const int64_t measuredFrameMicro = renderTimeTotalMicro / framesRendered;
                        if ((currentTimeMicro > expectedTimeMicro) || ((currentTimeMicro + measuredFrameMicro) > adjustedTimeWindowMicro)) {
                            displayTicks += workload.viOriginalRate;
                            skippedFrames = true;
                            snapdiag::subFrameDroppedCounter().fetch_add(1, std::memory_order_relaxed);
                            continue;
                        }
                    }

                    RenderTarget *overrideTarget = nullptr;
                    uint32_t overrideModifier = 0;
                    if (generateInterpolatedFrames) {
                        prevFrameWeight = std::clamp((workloadConfig.targetRate + displayTicks - logicalTicks) / float(workloadConfig.targetRate), 0.0f, 1.0f);
                        displayTicks += workload.viOriginalRate;
                        curFrameWeight = std::clamp((workloadConfig.targetRate + displayTicks - logicalTicks) / float(workloadConfig.targetRate), 0.0f, 1.0f);

                        // Every interpolated frame of a tick must sit further
                        // along the motion than the one before it. If a weight
                        // ever moves backwards, two poses of the same motion
                        // reach the screen out of order and are seen as two of
                        // everything.
                        if (snapdiag::statsEnabled()) {
                            static float lastPresentedWeight = 0.0f;
                            if ((frame > 0) && (curFrameWeight < lastPresentedWeight - 1e-4f)) {
                                snapdiag::weightWentBackwardsCounter().fetch_add(1, std::memory_order_relaxed);
                            }
                            lastPresentedWeight = curFrameWeight;
                        }


                        // Pokemon Snap port: no whole-frame cut handling here.
                        // Cuts are declared per transform through the display
                        // list -- the camera's matrix group skips its
                        // components on the frame the game's own camera data
                        // jumped (src/matrix_tags.cpp), covering scripted cuts
                        // and block-transition origin moves alike, and
                        // geometry that appears or disappears simply goes
                        // unpaired and draws at its current pose. Only what
                        // actually cut snaps; everything else keeps
                        // interpolating, so a cut costs a single native-rate
                        // step of the camera instead of a frame of frozen
                        // motion. Forcing the whole interval's weights here
                        // was the old mechanism, and its hold WAS the stutter
                        // reported at every transition.

                        // Override the render target.
                        if (usingMSAA || (frame > 0)) {
                            overrideTarget = interpolatedTargets[targetIndex].get();
                            overrideModifier = (targetIndex + 1);

                            if (useDifferentCounters && (prevFrameCounters.available > 0) && (targetIndex < prevFrameCounters.available)) {
                                // Wait until the target has finished presenting if the alternate frame counter (used by the present queue) is making use of this target.
                                std::unique_lock<std::mutex> interpolatedLock(ext.sharedResources->interpolatedMutex);
                                ext.sharedResources->interpolatedCondition.wait(interpolatedLock, [&]() {
                                    frameReduction = frameReduction || (prevFrameCounters.presented <= targetIndex);
                                    return prevFrameCounters.presented > targetIndex;
                                });
                            }

                            targetIndex++;
                        }
                    }
                    else if (workload.paused) {
                        curFrameWeight = workload.debuggerRenderer.interpolationWeight;
                        prevFrameWeight = 1.0f - curFrameWeight;
                    }
                    else {
                        prevFrameWeight = 0.0f;
                        curFrameWeight = 1.0f;
                    }

                    const bool uploadExtras = (frame == 0) && workloadConfig.raytracingEnabled;
                    if (uploadExtras) {
                        BufferUploader::Upload extrasUpload = { workload.drawData.extraParams.data(), { 0, workload.drawData.extraParams.size() }, sizeof(interop::ExtraParams), RenderBufferFlag::STORAGE, {}, &workload.drawBuffers.extraParamsBuffer };
                        ext.workloadExtrasUploader->submit(ext.workloadGraphicsWorker, { extrasUpload });
                    }

                    int64_t renderTimeMicro = workloadTimer.elapsedMicroseconds();
                    const uint32_t materialsBefore = snapdiag::shaderAskedCounter().load(std::memory_order_relaxed);

                    // A held interval's extra images are the previous frame
                    // repeated; only frame zero renders (the game reads its
                    // framebuffer back, so the transit frame must exist in
                    // RDRAM exactly as the game computed it), and its
                    // presented image is then replaced with the previous
                    // frame's before the present thread is told about it.
                    const bool heldSubFrame = snapCutHold && (frame > 0) &&
                        threadHoldCopy(snapHoldScratch.get(), RenderTargetKey(), overrideTarget, RenderTargetKey());
                    if (!heldSubFrame) {
                        const bool interpolationSubFrame = generateInterpolatedFrames && (curFrameWeight < 1.0f);
                        threadRenderFrame(curFrame, prevFrame, workloadConfig, workload.debuggerRenderer, workload.debuggerCamera, curFrameWeight, prevFrameWeight, deltaTimeMs,
                            interpolationTargetKey, interpolationTargetFbPairIndex, overrideTarget, overrideModifier, velocityUploaderUsed, uploadExtras, tileInterpolationUsed, lookAtInterpolationUsed,
                            interpolationSubFrame);
                    }

                    if (snapCutHold && (frame == 0) && (overrideTarget == nullptr)) {
                        threadHoldCopy(snapHoldScratch.get(), RenderTargetKey(), nullptr, interpolationTargetKey);
                    }

                    // Add total time the frame took to render.
                    const int64_t frameRenderMicro = workloadTimer.elapsedMicroseconds() - renderTimeMicro;
                    renderTimeTotalMicro += frameRenderMicro;

                    // A single rendered frame taking several frames' worth of
                    // time is where a hitch is actually spent. Reported with
                    // how many materials this frame was the first to ask for,
                    // which separates "the scene got heavier" from "the driver
                    // was busy turning new materials into pipelines".
                    if (snapdiag::statsEnabled() && (frameRenderMicro > 15000)) {
                        fprintf(stdout, "[SNAP-SLOWFRAME] rendering one frame took %.1f ms, new materials first seen this frame %u, draws %u\n",
                            double(frameRenderMicro) / 1000.0,
                            snapdiag::shaderAskedCounter().load(std::memory_order_relaxed) - materialsBefore,
                            workload.gameCallCount);
                        fflush(stdout);
                    }

                    // After one frame is rendered, we indicate the workload has been processed so the present thread can start presenting frames as soon as it can.
                    if (frame == 0) {
                        threadAdvanceWorkloadId(workload.workloadId);
                    }

                    // For every additional frame, we increase the frames available and notify the present queue.
                    if (generateInterpolatedFrames && (usingMSAA || (frame > 0))) {
                        {
                            std::scoped_lock<std::mutex> cursorLock(cursorMutex);
                            skipWorkloadNow = ((frame + 1) < displayFrames) && (writeCursor != threadCursor);
                        }

                        {
                            std::scoped_lock<std::mutex> managerLock(ext.sharedResources->interpolatedMutex);
                            curFrameCounters.skipped = skipWorkloadNow;
                            curFrameCounters.available++;
                        }

                        // Add the amount of display ticks that correspond to the remaining frames.
                        if (skipWorkloadNow) {
                            displayTicks += workload.viOriginalRate * (displayFrames - (frame + 1));
                            snapdiag::workloadDroppedCounter().fetch_add(displayFrames - (frame + 1), std::memory_order_relaxed);
                        }

                        ext.sharedResources->interpolatedCondition.notify_all();
                    }

                    framesRendered++;
                }

                // Set the skipped parameter on the frame counter if the workload wasn't skipped but some of its frames were.
                if (skippedFrames && !skipWorkloadNow) {
                    {
                        std::scoped_lock<std::mutex> managerLock(ext.sharedResources->interpolatedMutex);
                        curFrameCounters.skipped = true;
                    }

                    ext.sharedResources->interpolatedCondition.notify_all();
                }

                threadConfigurationValidate();

                // Remember which target this frame presented; a cut-transit
                // hold on the next frame shows this image again. A held frame
                // does not become the hold source itself -- its target shows
                // the previous image, which is exactly what a second
                // consecutive transit should keep showing.
                if (!workload.paused && !interpolationTargetKey.isEmpty()) {
                    snapPrevTargetKey = interpolationTargetKey;
                    // How many interpolated targets this frame filled, so a
                    // hold on the next one can take the last picture shown
                    // rather than this frame's opening pose. A held frame does
                    // not become a source itself: its targets carry the image
                    // it was holding, which is what a second consecutive held
                    // frame should keep showing.
                    if (!snapCutHold) {
                        snapPrevInterpolatedIndex = targetIndex;
                    }
                }

                if (!workload.paused) {
                    threadAdvanceBarrier();
                }

                processCursor = -1;
                workloadProfiler.end();
                workloadProfiler.log();
                workloadProfiler.reset();
            }
        }
    }
    
    void WorkloadQueue::idleThreadLoop() {
        // Beware traveler as you enter the zone of dirty driver hacks. Given N64 games are not exactly a demanding thing to render
        // nowadays for modern GPUs and due to how the plugin's cooperative multiqueue system works, it's sometimes just not possible
        // to keep the GPU busy at all times. It is often the case that the GPU might've already rendered all the frames it needed to
        // generate before the screen update event from the emulator even arrives on time.
        // 
        // Under this situation, some drivers are a bit too trigger-happy to downclock the GPU and lower the power consumption, eventually
        // resulting in very low power states that cause unwanted frametime spikes that can no longer reach the target framerate. This
        // results in visible judder during gameplay.
        //
        // This thread will take care of sending some GPU work that does nothing useful while the GPU is not actually busy generating
        // new frames. The waiting interval is close to the minimum resolution the OS provides and big enough to not cause any significant
        // delays or unwanted power consumption: it's just enough to keep the driver from downclocking to a power state level that is
        // usually intended for 2D work or video playback.
        //
        // This workaround is not required if the driver is configured to be at the "Max Performance" power state.

        Thread::setCurrentThreadName("RT64 Idle");

        const ShaderRecord &idle = ext.shaderLibrary->idle;
        RenderCommandList *commandList = ext.workloadGraphicsWorker->commandList.get();
        while (threadsRunning) {
            {
                std::unique_lock<std::mutex> idleLock(idleMutex);
                idleCondition.wait(idleLock, [&]() {
                    return idleActive || !threadsRunning;
                });
            }

            if (threadsRunning) {
                if (workerMutex.try_lock()) {
                    commandList->begin();
                    commandList->setPipeline(idle.pipeline.get());
                    commandList->setComputePipelineLayout(idle.pipelineLayout.get());
                    commandList->dispatch(1, 1, 1);
                    commandList->end();
                    ext.workloadGraphicsWorker->execute();
                    ext.workloadGraphicsWorker->wait();
                    workerMutex.unlock();
                }
                
                Thread::sleepMilliseconds(1);
            }
        }
    }
};