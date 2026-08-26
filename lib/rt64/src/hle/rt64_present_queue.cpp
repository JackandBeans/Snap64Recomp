//
// RT64
//

#include "rt64_present_queue.h"

#include "common/rt64_thread.h"
#include "rhi/rt64_render_hooks.h"

#include "rt64_workload_queue.h"
#include "rt64_snap_diag.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

// Pokemon Snap port: how many more presented images to photograph. Armed from
// the game side (src/matrix_tags.cpp, src/anim_steps.cpp) and from the workload
// queue on churn frames, and counted down here as the window is consumed. What
// reaches the screen is only ever assembled on this thread -- interpolated
// frames never touch RDRAM -- so this is the only place the picture a player
// actually saw can be captured. Atomic: several threads arm it.
extern "C" std::atomic<int32_t> snap_frame_dump_pending{0};

namespace RT64 {

namespace {
    // Written at half resolution: the artifact is full-screen scale and half
    // res keeps a burst of captures in the tens of megabytes.
    constexpr uint32_t SnapCaptureMaxFiles = 400;
    constexpr size_t SnapCaptureMaxQueuedJobs = 4;

    // Encoding a capture is a full-image conversion plus a multi-megabyte
    // file write. The first version of this rig did that on the present
    // thread between the fence wait and the present, and its stalls dropped
    // the very frames under investigation. The present thread now only copies
    // the mapped readback into a job; a worker owns the slow part. The job
    // queue and its thread are deliberately leaked: they may still be busy
    // when the process exits, and static teardown racing a detached worker
    // is a worse ending than a few pages reclaimed by the OS either way.
    struct SnapCaptureJob {
        std::vector<uint8_t> pixels;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t rowPitchBytes = 0;
        uint32_t bytesPerPixel = 0;
        bool sourceIsBGRA = false;
        uint32_t index = 0;
    };

    struct SnapPresentCapture {
        std::unique_ptr<RenderBuffer> buffer;
        uint64_t bufferSize = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t rowPitchBytes = 0;
        uint32_t bytesPerPixel = 0;
        std::atomic<uint32_t> filesWritten{0};
        uint32_t counter = 0;
        bool pending = false;
        bool warned = false;
        bool workerStarted = false;
        std::mutex jobMutex;
        std::condition_variable jobCondition;
        std::deque<SnapCaptureJob> jobs;
    };
    SnapPresentCapture &snapCapture() {
        static SnapPresentCapture *capture = new SnapPresentCapture();
        return *capture;
    }

    // The formats the interpolated color targets actually use. RT64 renders
    // in 16-bit unorm color by default, so that one matters most. The 16-bit
    // targets are full-scale unorm (measured: bright frames reach 0xFFFF), so
    // the high byte of each channel is the eight-bit image; the old
    // per-frame scale guess misdecoded legitimately dark frames.
    uint32_t snapCaptureBytesPerPixel(RenderFormat format) {
        switch (format) {
            case RenderFormat::R8G8B8A8_UNORM:
            case RenderFormat::B8G8R8A8_UNORM:
                return 4;
            case RenderFormat::R16G16B16A16_UNORM:
                return 8;
            default:
                return 0;
        }
    }

    // Runs on the capture worker: half-res conversion to BGR, then the shared
    // writer. Success is counted here, where it is known.
    void snapCaptureEncodeJob(const SnapCaptureJob &job) {
        const uint32_t outWidth = job.width / 2;
        const uint32_t outHeight = job.height / 2;
        if ((outWidth == 0) || (outHeight == 0) || !snapdiag::ensureDumpDir()) {
            return;
        }

        std::vector<uint8_t> bgr(size_t(outWidth) * outHeight * 3);
        for (uint32_t y = 0; y < outHeight; y++) {
            const uint8_t *src = job.pixels.data() + uint64_t(y) * 2 * job.rowPitchBytes;
            uint8_t *out = bgr.data() + size_t(y) * outWidth * 3;
            for (uint32_t x = 0; x < outWidth; x++) {
                const uint8_t *p = src + uint64_t(x) * 2 * job.bytesPerPixel;
                if (job.bytesPerPixel == 8) {
                    out[x * 3 + 0] = p[5];
                    out[x * 3 + 1] = p[3];
                    out[x * 3 + 2] = p[1];
                }
                else if (job.sourceIsBGRA) {
                    out[x * 3 + 0] = p[0];
                    out[x * 3 + 1] = p[1];
                    out[x * 3 + 2] = p[2];
                }
                else {
                    out[x * 3 + 0] = p[2];
                    out[x * 3 + 1] = p[1];
                    out[x * 3 + 2] = p[0];
                }
            }
        }

        char path[160];
        snprintf(path, sizeof(path), "snap_frame_dumps/r%05u_present_%05u.bmp", snapdiag::runToken(), job.index);
        if (snapdiag::writeBMP24(path, outWidth, outHeight, bgr.data())) {
            snapCapture().filesWritten.fetch_add(1);
            fprintf(stdout, "[SNAP-PCAP] wrote present %u\n", job.index);
            fflush(stdout);
        }
    }

    void snapCaptureEnqueue(SnapCaptureJob &&job) {
        SnapPresentCapture &capture = snapCapture();
        std::unique_lock<std::mutex> lock(capture.jobMutex);
        if (!capture.workerStarted) {
            capture.workerStarted = true;
            std::thread([]() {
                SnapPresentCapture &worker = snapCapture();
                while (true) {
                    SnapCaptureJob job;
                    {
                        std::unique_lock<std::mutex> workerLock(worker.jobMutex);
                        worker.jobCondition.wait(workerLock, [&]() { return !worker.jobs.empty(); });
                        job = std::move(worker.jobs.front());
                        worker.jobs.pop_front();
                    }
                    snapCaptureEncodeJob(job);
                }
            }).detach();
        }

        // Bounded: a burst that outruns the disk drops frames rather than
        // ballooning memory; the drop is visible as a numbering gap.
        if (capture.jobs.size() < SnapCaptureMaxQueuedJobs) {
            capture.jobs.emplace_back(std::move(job));
            lock.unlock();
            capture.jobCondition.notify_one();
        }
    }

    // Records a copy of the texture the VI is about to draw into a readback
    // buffer on the open command list. The caller's existing execute + wait
    // makes the buffer safe to map afterwards.
    void snapCaptureRecord(RenderDevice *device, RenderCommandList *commandList, const RenderTexture *texture, RenderFormat format, uint32_t width, uint32_t height) {
        SnapPresentCapture &capture = snapCapture();
        if (capture.warned || (capture.filesWritten.load() >= SnapCaptureMaxFiles)) {
            return;
        }

        const uint32_t bytesPerPixel = snapCaptureBytesPerPixel(format);
        if (bytesPerPixel == 0) {
            capture.warned = true;
            fprintf(stdout, "[SNAP-PCAP] present format %u not supported, captures disabled\n", uint32_t(format));
            fflush(stdout);
            return;
        }

        // D3D12 requires the row pitch aligned to 256 bytes.
        const uint32_t alignPixels = 256 / bytesPerPixel;
        const uint32_t alignedWidth = (width + alignPixels - 1) & ~(alignPixels - 1);
        const uint64_t requiredSize = uint64_t(alignedWidth) * height * bytesPerPixel;
        if ((capture.buffer == nullptr) || (capture.bufferSize < requiredSize)) {
            // Safe to replace here: every prior present executed and waited on
            // its command list, so no recorded copy still references the old
            // buffer.
            capture.buffer = device->createBuffer(RenderBufferDesc::ReadbackBuffer(requiredSize));
            capture.bufferSize = requiredSize;
            if (capture.buffer == nullptr) {
                // The last capture rig bug this port shipped was a null
                // flowing into the driver; don't grow another one.
                capture.warned = true;
                capture.bufferSize = 0;
                fprintf(stdout, "[SNAP-PCAP] readback allocation failed, captures disabled\n");
                fflush(stdout);
                return;
            }
        }

        RenderTextureBarrier toCopy(const_cast<RenderTexture *>(texture), RenderTextureLayout::COPY_SOURCE);
        RenderBufferBarrier bufferWrite(capture.buffer.get(), RenderBufferAccess::WRITE);
        commandList->barriers(RenderBarrierStage::COPY, &bufferWrite, 1, &toCopy, 1);
        commandList->copyTextureRegion(
            RenderTextureCopyLocation::PlacedFootprint(capture.buffer.get(), format, width, height, 1, alignedWidth),
            RenderTextureCopyLocation::Subresource(texture));
        commandList->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(const_cast<RenderTexture *>(texture), RenderTextureLayout::SHADER_READ));

        capture.width = width;
        capture.height = height;
        capture.rowPitchBytes = alignedWidth * bytesPerPixel;
        capture.bytesPerPixel = bytesPerPixel;
        capture.pending = true;
    }

    // Maps the readback, hands the pixels to the worker, and returns. Only
    // called after the present worker's fence wait, which is what makes the
    // map safe; everything slow happens off this thread.
    void snapCaptureFinish(RenderFormat format) {
        SnapPresentCapture &capture = snapCapture();
        if (!capture.pending) {
            return;
        }
        capture.pending = false;

        RenderRange readRange(0, capture.bufferSize);
        const uint8_t *pixels = reinterpret_cast<const uint8_t *>(capture.buffer->map(0, &readRange));
        if (pixels == nullptr) {
            return;
        }

        SnapCaptureJob job;
        job.width = capture.width;
        job.height = capture.height;
        job.rowPitchBytes = capture.rowPitchBytes;
        job.bytesPerPixel = capture.bytesPerPixel;
        job.sourceIsBGRA = (format == RenderFormat::B8G8R8A8_UNORM);
        job.index = capture.counter++;
        job.pixels.assign(pixels, pixels + capture.bufferSize);
        capture.buffer->unmap();

        snapCaptureEnqueue(std::move(job));
    }
}

    // PresentQueue

    PresentQueue::PresentQueue() {
        reset();
    }

    PresentQueue::~PresentQueue() {
        presentThreadRunning = false;
        cursorCondition.notify_all();

        if (presentThread != nullptr) {
            presentThread->join();
            delete presentThread;
        }

        presentIdCondition.notify_all();
    }

    void PresentQueue::reset() {
        threadCursor = 0;
        writeCursor = 0;
        barrierCursor = 0;
        presentId = 0;
    }

    void PresentQueue::advanceToNextPresent() {
        int nextWriteCursor = (writeCursor + 1) % presents.size();

        // Stall the thread until the barrier is lifted if we're trying to write on a present being used by the GPU.
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

    void PresentQueue::repeatLastPresent() {
        {
            const std::scoped_lock lock(cursorMutex);
            threadCursor = previousWriteCursor();
        }

        cursorCondition.notify_all();
    }

    uint32_t PresentQueue::previousWriteCursor() const {
        if (writeCursor > 0) {
            return writeCursor - 1;
        }
        else {
            return uint32_t(presents.size()) - 1;
        }
    }

    void PresentQueue::waitForIdle() {
        std::unique_lock<std::mutex> threadLock(threadMutex);
    }

    void PresentQueue::waitForPresentId(uint64_t waitId) {
        std::unique_lock<std::mutex> presentLock(presentIdMutex);
        presentIdCondition.wait(presentLock, [&]() {
            return (waitId <= presentId) || !presentThreadRunning;
        });
    }

    void PresentQueue::setup(const External &ext) {
        this->ext = ext;

        viRenderer = std::make_unique<VIRenderer>();

        presentThreadRunning = true;
        presentThread = new std::thread(&PresentQueue::threadLoop, this);
    }

    void PresentQueue::threadPresent(const Present &present, bool &swapChainValid) {
        FramebufferManager &fbManager = ext.sharedResources->framebufferManager;
        RenderTargetManager &targetManager = ext.sharedResources->renderTargetManager;
        const bool usingMSAA = (targetManager.multisampling.sampleCount > 1);
        hlslpp::float2 resolutionScale;
        EnhancementConfiguration::Presentation::Mode presentationMode;
        bool removeBlackBorders;
        uint32_t overscanCrop[4];
        UserConfiguration::RefreshRate refreshRate;
        UserConfiguration::Filtering filtering;
        uint32_t viOriginalRate;
        uint32_t targetRate;
        {
            std::scoped_lock<std::mutex> configurationLock(ext.sharedResources->configurationMutex);
            resolutionScale = ext.sharedResources->resolutionScale;
            presentationMode = ext.sharedResources->enhancementConfig.presentation.mode;
            removeBlackBorders = ext.sharedResources->enhancementConfig.presentation.removeBlackBorders;
            for (uint32_t i = 0; i < 4; i++) {
                overscanCrop[i] = ext.sharedResources->enhancementConfig.presentation.crop[i];
            }
            refreshRate = ext.sharedResources->userConfig.refreshRate;
            filtering = ext.sharedResources->userConfig.filtering;
            viOriginalRate = ext.sharedResources->viOriginalRate;
            targetRate = ext.sharedResources->targetRate;
        }

        RenderTarget *colorTarget = nullptr;
        int32_t framesToPresent = 1;
        bool lockedWorkloadMutex = false;
        InterpolatedFrameCounters &frameCounters = ext.sharedResources->interpolatedFrames[ext.sharedResources->interpolatedFramesIndex];

        // TODO: There's a possible race condition interactions that can happen while the workload
        // queue is rendering extra frames and the present event is processed while it's generating
        // interpolated frames. When the framebuffer manager or the render target manager maps are
        // modified while the present queue is retrieving the framebuffer or the target. These can
        // likely be solved by locking the access to the managers during modification.
        
        // Perform any external write operations indicated by the event.
        if (!present.fbOperations.empty()) {
            const std::scoped_lock lock(screenFbChangePoolMutex);
            {
                RenderWorkerExecution workerExecution(ext.presentGraphicsWorker);
                fbManager.performOperations(ext.presentGraphicsWorker, &screenFbChangePool, nullptr, ext.shaderLibrary, nullptr,
                    present.fbOperations, targetManager, resolutionScale, 0, 0, nullptr);
            }
        }

        // Present the VI specified by the event.
        // Attempt to find the matching framebuffer for the VI based on the origin address.
        // If that fails, we look at the shared storage.
        if (present.screenVI.visible()) {
            Framebuffer *viFb = nullptr;
            if (!viewRDRAM) {
                viFb = fbManager.find(present.screenVI.fbAddress());
            }

            Framebuffer *presentFb = viFb;
            
            // Show the framebuffer the debugger has requested instead.
            if (present.debuggerFramebuffer.view) {
                Framebuffer *candidateFb = fbManager.find(present.debuggerFramebuffer.address);
                if (candidateFb != nullptr) {
                    presentFb = candidateFb;
                }
            }
            
            if ((presentFb != nullptr) && (viFb != nullptr)) {
                for (uint32_t colorAddress : ext.sharedResources->colorImageAddressVector) {
                    Framebuffer *colorFb = fbManager.find(colorAddress);
                    if (colorFb == nullptr) {
                        continue;
                    }

                    // Always default to interpolation being disabled for all modified framebuffers.
                    colorFb->interpolationEnabled = false;
                    
                    // When the skip buffering option is on, we check the video history to find if any of the framebuffers that
                    // were drawn in this frame have been previously used for presentation. This is ignored when the debugger
                    // has forced viewing a particular framebuffer.
                    if (!present.debuggerFramebuffer.view && (presentationMode == EnhancementConfiguration::Presentation::Mode::SkipBuffering)) {
                        for (size_t h = 0; h < viHistory.history.size(); h++) {
                            const VIHistory::Present &entry = viHistory.history[h];
                            if ((colorFb->addressStart == entry.vi.fbAddress()) && (colorFb->width == entry.fbWidth) && (colorFb->siz == entry.vi.fbSiz()) && entry.vi.compatibleWith(present.screenVI)) {
                                presentFb = colorFb;
                                break;
                            }
                        }
                    }

                    // Present early (or games that behave like it) will make it so that the presented image is a color image
                    // that the workload modified. We run a basic check to see if that holds true to indicate it was presented
                    // so interpolation is possible.
                    if (colorFb == presentFb) {
                        presentFb->interpolationEnabled = true;
                        break;
                    }
                }

                if (presentFb->interpolationEnabled) {
                    framesToPresent = frameCounters.count;
                }
                else {
                    // The renderer produced a whole tick of interpolated
                    // frames and none of them can be shown, because the buffer
                    // being presented could not be matched to the one drawn
                    // into. The tick presents a single image instead, and the
                    // picture stands still for its whole duration.
                    if ((frameCounters.count > 1) && snapdiag::statsEnabled()) {
                        snapdiag::interpolationUnusedCounter().fetch_add(1, std::memory_order_relaxed);
                    }
                    lockedWorkloadMutex = true;
                    ext.sharedResources->workloadMutex.lock();
                }

                // Pokemon Snap port, diagnostic: a frame that cannot be
                // interpolated is presented once, raw -- a pacing hiccup at
                // the display rate, which is a stutter by construction. If
                // these land on the frames that flash, the presentation path
                // is the flash; if the presented address is ever not one of
                // the game's two display buffers, the wrong image is being
                // shown outright.
                // Pokemon Snap port, diagnostic: the game's framebuffer
                // carries mode-specific dead margins (measured L14/R16/T12/B8
                // in play, L30/T20/B20 in the intro's cinematics), and whether
                // the VI compensates by shifting its scan window decides the
                // correct presentation fix. Print the registers per change.
                if (snapdiag::diagEnabled()) {
                    static VI lastVI = {};
                    const VI &svi = present.screenVI;
                    // The origin alternates between the two display buffers
                    // every present; a change log that includes it prints
                    // every frame and buries the mode changes it exists for.
                    VI originMasked = svi;
                    originMasked.origin = lastVI.origin;
                    if (originMasked != lastVI) {
                        lastVI = svi;
                        fprintf(stdout, "[SNAP-VI] w %u h %u,%u v %u,%u xs %u xo %u ys %u yo %u origin %08X\n",
                            svi.width, svi.hRegion.hStart, svi.hRegion.hEnd, svi.vRegion.vStart, svi.vRegion.vEnd,
                            svi.xTransform.xScale, svi.xTransform.xOffset, svi.yTransform.yScale, svi.yTransform.yOffset,
                            svi.origin);
                        fflush(stdout);
                    }
                }


                RenderTargetKey colorTargetKey(presentFb->addressStart, presentFb->width, presentFb->siz, Framebuffer::Type::Color);
                colorTarget = &targetManager.get(colorTargetKey, true);
                if (!colorTarget->isEmpty()) {
                    // If a depth framebuffer is about to be shown, convert it to color.
                    if (presentFb->isLastWriteDifferent(Framebuffer::Type::Color)) {
                        RenderTargetKey otherColorTargetKey(presentFb->addressStart, presentFb->width, presentFb->siz, presentFb->lastWriteType);
                        RenderTarget &otherColorTarget = targetManager.get(otherColorTargetKey, true);
                        if (!otherColorTarget.isEmpty()) {
                            const FixedRect &r = presentFb->lastWriteRect;
                            RenderWorkerExecution workerExecution(ext.presentGraphicsWorker);
                            colorTarget->copyFromTarget(ext.presentGraphicsWorker, &otherColorTarget, r.left(false), r.top(false), r.width(false, true), r.height(false, true), ext.shaderLibrary);
                        }
                    }
                }
                else {
                    colorTarget = nullptr;
                }

                if (!present.paused && (viHistory.top().vi != present.screenVI)) {
                    viHistory.pushVI(present.screenVI, viFb->width);
                }
            }
            else {
                uint32_t fbAddress = present.screenVI.fbAddress();

                // Use a scratch framebuffer to upload the RAM to the render target.
                hlslpp::uint2 fbSize = present.screenVI.fbSize();
                scratchFb.addressStart = fbAddress;
                scratchFb.width = fbSize.x;
                scratchFb.height = fbSize.y;
                scratchFb.siz = present.screenVI.fbSiz();

                lockedWorkloadMutex = true;
                ext.sharedResources->workloadMutex.lock();

                RenderTargetKey colorTargetKey(fbAddress, scratchFb.width, scratchFb.siz, Framebuffer::Type::Color);
                colorTarget = &targetManager.get(colorTargetKey, true);
                colorTarget->resize(ext.presentGraphicsWorker, scratchFb.width, scratchFb.height);
                colorTarget->resolutionScale = { 1.0f, 1.0f };
                colorTarget->downsampleMultiplier = 1;

                scratchFb.nativeTarget.resetBufferHistory();

                {
                    RenderWorkerExecution workerExecution(ext.presentGraphicsWorker);
                    colorTarget->clearColorTarget(ext.presentGraphicsWorker);
                    FramebufferChange *colorFbChange = scratchFb.readChangeFromBytes(ext.presentGraphicsWorker, scratchFbChangePool, Framebuffer::Type::Color,
                        G_IM_FMT_RGBA, present.storage.data(), 0, scratchFb.height, ext.shaderLibrary);

                    if (colorFbChange != nullptr) {
                        colorTarget->copyFromChanges(ext.presentGraphicsWorker, *colorFbChange, scratchFb.width, scratchFb.height, 0, ext.shaderLibrary);
                    }
                }

                scratchFbChangePool.reset();

                if (!present.paused && (viHistory.top().vi != present.screenVI)) {
                    viHistory.pushVI(present.screenVI, fbSize.x);
                }
            }
        }

        // Create the framebuffers if necessary.
        if (swapChainFramebuffers.empty()) {
            uint32_t textureCount = ext.swapChain->getTextureCount();
            swapChainFramebuffers.resize(textureCount);
            for (uint32_t i = 0; i < textureCount; i++) {
                const RenderTexture *swapChainTexture = ext.swapChain->getTexture(i);
                swapChainFramebuffers[i] = ext.device->createFramebuffer(RenderFramebufferDesc(&swapChainTexture, 1));
            }
        }
        
        for (int32_t i = 0; i < framesToPresent; i++) {
            uint32_t frameCountersNextPresented = 0;
            if ((framesToPresent > 1) && (usingMSAA || (i > 0))) {
                // Stall until the interpolated color target is available.
                const uint32_t targetIndex = usingMSAA ? i : (i - 1);
                std::unique_lock<std::mutex> interpolatedLock(ext.sharedResources->interpolatedMutex);
                ext.sharedResources->interpolatedCondition.wait(interpolatedLock, [&]() {
                    return (frameCounters.available > targetIndex) || ((frameCounters.available == targetIndex) && frameCounters.skipped);
                });

                // Do not present any more frames after this one after reaching the last available frame if the workload was skipped.
                if ((frameCounters.available == targetIndex) && frameCounters.skipped) {
                    framesToPresent = std::min(int(frameCounters.available), i + 1);
                    frameCountersNextPresented = frameCounters.count;
                }
                else {
                    frameCountersNextPresented = frameCounters.presented + 1;
                }

                if (i < framesToPresent) {
                    uint32_t targetIndex = usingMSAA ? i : (i - 1);
                    colorTarget = ext.sharedResources->interpolatedColorTargets[targetIndex].get();
                }
                else {
                    colorTarget = nullptr;
                }
            }
            else if (framesToPresent == 1) {
                frameCountersNextPresented = frameCounters.count;
            }

            uint32_t swapChainIndex = 0;
            const bool presentFrame = (i < framesToPresent) && swapChainValid;
            if (presentFrame) {
                swapChainValid = ext.swapChain->acquireTexture(acquiredSemaphore.get(), &swapChainIndex);
            }

            if (presentFrame && swapChainValid) {
                // Draw the framebuffer with the VI renderer.
                RenderTexture *swapChainTexture = ext.swapChain->getTexture(swapChainIndex);
                RenderFramebuffer *swapChainFramebuffer = swapChainFramebuffers[swapChainIndex].get();
                RenderCommandList *commandList = ext.presentGraphicsWorker->commandList.get();
                commandList->begin();
                commandList->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(swapChainTexture, RenderTextureLayout::COLOR_WRITE));
                
                VIRenderer::RenderParams renderParams;
                if (colorTarget != nullptr) {
                    renderParams.device = ext.device;
                    renderParams.commandList = commandList;
                    renderParams.swapChain = ext.swapChain;
                    renderParams.shaderLibrary = ext.shaderLibrary;
                    renderParams.textureFormat = colorTarget->format;
                    renderParams.resolutionScale = colorTarget->resolutionScale;
                    renderParams.downsamplingScale = 1;
                    renderParams.filtering = filtering;
                    renderParams.vi = &present.screenVI;
                    renderParams.removeBlackBorders = removeBlackBorders;
                    for (uint32_t i = 0; i < 4; i++) {
                        renderParams.crop[i] = overscanCrop[i];
                    }

                    const bool useDownsampling = (colorTarget->downsampleMultiplier > 1);
                    if (useDownsampling) {
                        colorTarget->downsampleTarget(ext.presentGraphicsWorker, ext.shaderLibrary);
                        renderParams.texture = colorTarget->downsampledTexture.get();
                        renderParams.textureWidth = colorTarget->width / colorTarget->downsampleMultiplier;
                        renderParams.textureHeight = colorTarget->height / colorTarget->downsampleMultiplier;
                        renderParams.downsamplingScale = colorTarget->downsampleMultiplier;
                    }
                    else {
                        colorTarget->resolveTarget(ext.presentGraphicsWorker, ext.shaderLibrary);
                        renderParams.texture = colorTarget->getResolvedTexture();
                        renderParams.textureWidth = colorTarget->width;
                        renderParams.textureHeight = colorTarget->height;
                    }
                }
                
                commandList->setFramebuffer(swapChainFramebuffer);
                commandList->clearColor();

                if (renderParams.texture != nullptr) {
                    commandList->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(renderParams.texture, RenderTextureLayout::SHADER_READ));
                    viRenderer->render(renderParams);

                    // Pokemon Snap port: while the game side is dumping its
                    // framebuffers around a churn frame, also photograph the
                    // image actually being presented, interpolation included.
                    if (snapdiag::captureEnabled() && (snap_frame_dump_pending.load() > 0)) {
                        // The window is consumed here. It used to be counted down by
                        // the game-side dumper, which no longer exists, so an armed
                        // capture never stopped until it hit the file cap.
                        snap_frame_dump_pending.fetch_sub(1, std::memory_order_relaxed);
                        if (snapdiag::diagEnabled()) {
                            fprintf(stdout, "[SNAP-PTEX] presenting %p (raw %p)\n",
                                (void *)renderParams.texture, (void *)colorTarget->texture.get());
                            fflush(stdout);
                        }
                        snapCaptureRecord(ext.device, commandList, renderParams.texture, renderParams.textureFormat,
                            renderParams.textureWidth, renderParams.textureHeight);
                    }
                }

                RenderHookDraw *drawHook = GetRenderHookDraw();
                if (drawHook != nullptr) {
                    drawHook(commandList, swapChainFramebuffer);
                }

                {
                    const std::scoped_lock lock(inspectorMutex);
                    if (inspector != nullptr) {
                        inspector->draw(commandList);
                    }
                    
                    commandList->barriers(RenderBarrierStage::NONE, RenderTextureBarrier(swapChainTexture, RenderTextureLayout::PRESENT));
                    commandList->end();
                    const RenderCommandList *commandList = ext.presentGraphicsWorker->commandList.get();
                    RenderCommandSemaphore *waitSemaphore = acquiredSemaphore.get();
                    RenderCommandSemaphore *signalSemaphore = drawSemaphores[swapChainIndex].get();
                    ext.presentGraphicsWorker->commandQueue->executeCommandLists(&commandList, 1, &waitSemaphore, 1, &signalSemaphore, 1, ext.presentGraphicsWorker->commandFence.get());
                    ext.presentGraphicsWorker->wait();
                }

                // The wait above is the fence for the recorded copy, so the
                // readback is safe to map and write out here.
                if (snapdiag::captureEnabled()) {
                    snapCaptureFinish(renderParams.textureFormat);
                }
            }

            if (lockedWorkloadMutex) {
                ext.sharedResources->workloadMutex.unlock();
                lockedWorkloadMutex = false;
            }
            
            if (frameCountersNextPresented > 0) {
                {
                    std::unique_lock<std::mutex> interpolatedLock(ext.sharedResources->interpolatedMutex);
                    frameCounters.presented = frameCountersNextPresented;
                }

                ext.sharedResources->interpolatedCondition.notify_all();
            }

            // As soon as we're done with the first render target, we notify the workload queue it can proceed.
            if (i == 0) {
                notifyPresentId(present);
            }

            if (presentFrame && swapChainValid) {
                // Wait until the approximate time the next present should be at the current intended rate.
                // Pokemon Snap port: when the port is interpolating to the
                // display's own rate, the display sets the cadence. Pacing
                // these presents from a software clock instead leaves two
                // clocks running at almost the same speed and never in step,
                // and they beat against each other: every few seconds a
                // present lands twice inside one refresh and is never seen,
                // or none lands and a refresh repeats. Frame after frame
                // arrives exactly on time by every internal measure while the
                // picture visibly stutters and doubles, worst during a smooth
                // pan where the eye tracks the motion. Handing the wait to the
                // display puts every frame on a refresh boundary, which is the
                // only way this is smooth at any rate the display runs at.
                const uint32_t displayRate = ext.sharedResources->swapChainRate;
                const bool displayPaced = (targetRate > 0) && (displayRate > 0) && (targetRate >= displayRate);
                if (displayPaced != swapChainDisplayPaced) {
                    ext.swapChain->setVsyncEnabled(displayPaced);
                    swapChainDisplayPaced = displayPaced;
                }

                if (!displayPaced && (presentTimestamp != Timestamp()) && (targetRate > 0) && (targetRate > viOriginalRate)) {
                    Timer::preciseSleepUntil(presentTimestamp + std::chrono::nanoseconds(1'000'000'000 / targetRate));
                }

                if (presentWaitEnabled) {
                    ext.swapChain->wait();
                }

                RenderCommandSemaphore *waitSemaphore = drawSemaphores[swapChainIndex].get();
                const Timestamp previousPresentTimestamp = presentTimestamp;
                presentTimestamp = Timer::current();
                swapChainValid = ext.swapChain->present(swapChainIndex, &waitSemaphore, 1);
                presentProfiler.logAndRestart();

                // Pokemon Snap port: how evenly frames actually reach the
                // screen, which is what stutter is. Judged here rather than
                // from frame counts, because a run can produce every frame it
                // owes and still look choppy if the intervals between them are
                // uneven. Accumulated in memory and summarised every few
                // hundred presents, so measuring costs nothing that could
                // itself cause the unevenness being measured -- the earlier
                // rigs disturbed the pacing far more than the faults they were
                // looking for.
                if (snapdiag::statsEnabled() && (previousPresentTimestamp != Timestamp())) {
                    static double intervalTotalMs = 0.0;
                    static double intervalWorstMs = 0.0;
                    static uint32_t intervalCount = 0;
                    static uint32_t intervalLate = 0;
                    static double intervalPrevMs = 0.0;
                    static uint32_t intervalJudder = 0;
                    static double ageTotalMs = 0.0;
                    static double ageWorstMs = 0.0;
                    static uint32_t ageCount = 0;
                    const int64_t newestStateNanos = snapdiag::newestStateNanos().load(std::memory_order_relaxed);
                    if (newestStateNanos != 0) {
                        // How far behind the game the picture is at the instant
                        // it goes out: the age of the newest state the game had
                        // computed when this frame was presented. Interpolation
                        // places frames between two game frames, so this cannot
                        // reach zero -- what it can show is everything queued
                        // on top of that, which is the part worth removing.
                        const int64_t presentNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(presentTimestamp.time_since_epoch()).count();
                        const double ageMs = double(presentNanos - newestStateNanos) / 1'000'000.0;
                        if ((ageMs >= 0.0) && (ageMs < 1000.0)) {
                            ageTotalMs += ageMs;
                            ageWorstMs = std::max(ageWorstMs, ageMs);
                            ageCount++;
                        }
                    }
                    // What the eye was actually shown. The image being presented
                    // carries how far through the world's motion it sits, so two
                    // consecutive presents subtract to how far the world moved
                    // between them. A present that advances the motion by nothing
                    // is a frame the player sees as a stall however punctually it
                    // arrived, and that is the difference between a port that
                    // measures healthy and one that feels smooth.
                    if (snapdiag::statsEnabled()) {
                        const uint32_t motionSlot = usingMSAA ? uint32_t(i + 1) : uint32_t(i);
                        if (motionSlot < snapdiag::SnapMotionSlots) {
                            const int64_t shownNow = snapdiag::motionSlots()[motionSlot].load(std::memory_order_relaxed);
                            const int64_t shownBefore = snapdiag::motionShownMicroFrames().exchange(shownNow, std::memory_order_relaxed);
                            if ((shownBefore != 0) && (shownNow != 0)) {
                                const int64_t step = shownNow - shownBefore;
                                if (step < 0) {
                                    // Two poses of the same motion reaching the
                                    // screen out of order, which is seen as two of
                                    // everything rather than as a pause.
                                    snapdiag::motionBackwardsCounter().fetch_add(1, std::memory_order_relaxed);
                                }
                                else if (step == 0) {
                                    snapdiag::motionStillPresentsCounter().fetch_add(1, std::memory_order_relaxed);
                                }
                                else {
                                    const uint32_t stepMicro = uint32_t(std::min<int64_t>(step, 0xFFFFFFFF));
                                    uint32_t biggest = snapdiag::motionBiggestStepMicro().load(std::memory_order_relaxed);
                                    while ((stepMicro > biggest) &&
                                        !snapdiag::motionBiggestStepMicro().compare_exchange_weak(biggest, stepMicro, std::memory_order_relaxed)) {
                                    }
                                }
                            }
                        }
                    }

                    const double intervalMs = std::chrono::duration<double, std::milli>(presentTimestamp - previousPresentTimestamp).count();

                    // A freeze long enough to be called a freeze reports
                    // itself the moment it happens, with what the renderer was
                    // busy with while the screen stood still. A summary every
                    // few hundred frames can say a stall happened; only this
                    // can say what caused it.
                    {
                        static uint32_t lastShaderAsked = 0;
                        static uint32_t lastShaderReady = 0;
                        const uint32_t shaderAsked = snapdiag::shaderAskedCounter().load(std::memory_order_relaxed);
                        const uint32_t shaderReady = snapdiag::shaderReadyCounter().load(std::memory_order_relaxed);
                        // A hiccup is a frame that took several frames' worth
                        // of time. At this display rate that is tens of
                        // milliseconds, not hundreds, and it is reported with
                        // what the renderer was doing and how old the newest
                        // game state was -- an old state means the game itself
                        // arrived late and nothing downstream could have
                        // helped, a fresh one means the delay was here.
                        if (intervalMs > 25.0) {
                            const int64_t stallStateNanos = snapdiag::newestStateNanos().load(std::memory_order_relaxed);
                            const int64_t presentNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(presentTimestamp.time_since_epoch()).count();
                            const double stateAgeMs = (stallStateNanos != 0) ? (double(presentNanos - stallStateNanos) / 1'000'000.0) : -1.0;
                            fprintf(stdout, "[SNAP-STALL] %.1f ms gap; newest game state was %.1f ms old (%s), new materials asked %u compiled %u\n",
                                intervalMs, stateAgeMs,
                                (stateAgeMs > intervalMs * 0.75) ? "game thread was late" : "delay was in the renderer",
                                shaderAsked - lastShaderAsked, shaderReady - lastShaderReady);
                            fprintf(stdout, "[SNAP-STALL]   so far: interpolation not presentable %u, single-image ticks %u, weights out of order %u\n",
                                snapdiag::interpolationUnusedCounter().load(std::memory_order_relaxed),
                                snapdiag::singleFrameTickCounter().load(std::memory_order_relaxed),
                                snapdiag::weightWentBackwardsCounter().load(std::memory_order_relaxed));
                            fflush(stdout);
                        }
                        lastShaderAsked = shaderAsked;
                        lastShaderReady = shaderReady;
                    }

                    intervalTotalMs += intervalMs;
                    intervalWorstMs = std::max(intervalWorstMs, intervalMs);
                    intervalCount++;
                    // A present that took more than twice the run's own average
                    // is a visible hitch; one that differs sharply from the
                    // present before it is the alternation the eye reads as
                    // judder even when the average looks healthy.
                    const double runningAverageMs = intervalTotalMs / intervalCount;
                    if (intervalMs > (runningAverageMs * 2.0)) {
                        intervalLate++;
                    }
                    if ((intervalPrevMs > 0.0) && (std::abs(intervalMs - intervalPrevMs) > (runningAverageMs * 0.5))) {
                        intervalJudder++;
                    }
                    intervalPrevMs = intervalMs;
                    if (intervalCount >= 600) {
                        const uint32_t asked = snapdiag::subFrameAskedCounter().exchange(0, std::memory_order_relaxed);
                        const uint32_t dropped = snapdiag::subFrameDroppedCounter().exchange(0, std::memory_order_relaxed) +
                            snapdiag::workloadDroppedCounter().exchange(0, std::memory_order_relaxed);
                        fprintf(stdout, "[SNAP-PACE] %u presents, average %.2f ms (%.1f fps), worst %.2f ms, hitches %u, uneven pairs %u, frames held %u, interpolated frames asked %u dropped %u (%.1f%%), picture age average %.2f ms worst %.2f ms\n",
                            intervalCount, runningAverageMs, 1000.0 / runningAverageMs, intervalWorstMs, intervalLate, intervalJudder,
                            snapdiag::holdCounter().exchange(0, std::memory_order_relaxed),
                            asked, dropped, (asked > 0) ? (100.0 * double(dropped) / double(asked)) : 0.0,
                            (ageCount > 0) ? (ageTotalMs / ageCount) : 0.0, ageWorstMs);
                        fprintf(stdout, "[SNAP-PACE]   holds asked by camera %u, by authored step %u; verdicts raised: camera %u, step %u of which isolated %u\n",
                            snapdiag::holdFromCameraCounter().exchange(0, std::memory_order_relaxed),
                            snapdiag::holdFromStepCounter().exchange(0, std::memory_order_relaxed),
                            snapdiag::cameraDeclaredCounter().exchange(0, std::memory_order_relaxed),
                            snapdiag::stepDeclaredCounter().exchange(0, std::memory_order_relaxed),
                            snapdiag::stepIsolatedCounter().exchange(0, std::memory_order_relaxed));
                        {
                            // How much of the scene the renderer could pair with the
                            // frame before it. Whatever it could not is drawn at one
                            // pose for the whole tick, so it steps once per game frame
                            // while everything around it glides -- which is most
                            // visible when the camera moves and the whole screen is in
                            // motion, and is what an effect sprite running at the
                            // game's rate against smooth geometry looks like.
                            const uint32_t seen = snapdiag::transformsSeenCounter().exchange(0, std::memory_order_relaxed);
                            const uint32_t paired = snapdiag::transformsPairedCounter().exchange(0, std::memory_order_relaxed);
                            const uint32_t rects = snapdiag::rectDrawCounter().exchange(0, std::memory_order_relaxed);
                            const uint32_t rectsPaired = snapdiag::rectsPairedCounter().exchange(0, std::memory_order_relaxed);
                            fprintf(stdout, "[SNAP-PACE]   scene pairing: %u of %u transforms paired (%.1f%%), sprites %u of %u paired (%.1f%%)\n",
                                paired, seen, (seen > 0) ? (100.0 * double(paired) / double(seen)) : 0.0,
                                rectsPaired, rects, (rects > 0) ? (100.0 * double(rectsPaired) / double(rects)) : 0.0);
                            fprintf(stdout, "[SNAP-PACE]   sprites left alone: %u frames for a scene change, %u groups for a membership change\n",
                                snapdiag::rectSceneSkipCounter().exchange(0, std::memory_order_relaxed),
                                snapdiag::rectGroupSkipCounter().exchange(0, std::memory_order_relaxed));
                            fprintf(stdout, "[SNAP-PACE]   sprite pairing cost %.1f ms over the interval\n",
                                double(snapdiag::rectMatchNanos().exchange(0, std::memory_order_relaxed)) / 1.0e6);
                            const uint32_t stillPresents = snapdiag::motionStillPresentsCounter().exchange(0, std::memory_order_relaxed);
                            const uint32_t backwards = snapdiag::motionBackwardsCounter().exchange(0, std::memory_order_relaxed);
                            const double biggestStep = double(snapdiag::motionBiggestStepMicro().exchange(0, std::memory_order_relaxed)) / 1000000.0;
                            fprintf(stdout, "[SNAP-PACE]   motion shown: %u of %u presents advanced the world by nothing (%.1f%%), %u went backwards, biggest single step %.2f game frames\n",
                                stillPresents, intervalCount,
                                (intervalCount > 0) ? (100.0 * double(stillPresents) / double(intervalCount)) : 0.0,
                                backwards, biggestStep);
                            // What the picture is actually being paced against.
                            // Presenting faster than the display can show is
                            // torn, unevenly spaced frames however good every
                            // other number here looks.
                            fprintf(stdout, "[SNAP-PACE]   display %u Hz, target %u, game %u, presenting %s\n",
                                ext.sharedResources->swapChainRate, targetRate, viOriginalRate,
                                ((targetRate > 0) && (ext.sharedResources->swapChainRate > 0) && (targetRate >= ext.sharedResources->swapChainRate)) ? "in step with the display" : "as fast as it can");
                            const uint32_t drawnFrames = snapdiag::drawnFrameCounter().exchange(0, std::memory_order_relaxed);
                            const uint32_t logicSteps = snapdiag::logicStepCounter().exchange(0, std::memory_order_relaxed);
                            const uint32_t skippedDraws = snapdiag::skippedDrawCounter().exchange(0, std::memory_order_relaxed);
                            fprintf(stdout, "[SNAP-PACE]   game frames %u carrying %u logic steps (%.2f each), of which %u carried more than usual because the game skipped a draw\n",
                                drawnFrames, logicSteps,
                                (drawnFrames > 0) ? (double(logicSteps) / double(drawnFrames)) : 0.0,
                                skippedDraws);
                        }
                        ageTotalMs = 0.0;
                        ageWorstMs = 0.0;
                        ageCount = 0;
                        fflush(stdout);
                        intervalTotalMs = 0.0;
                        intervalWorstMs = 0.0;
                        intervalCount = 0;
                        intervalLate = 0;
                        intervalJudder = 0;
                    }
                }
            }
        }
    }

    void PresentQueue::skipInterpolation() {
        {
            std::unique_lock<std::mutex> interpolatedLock(ext.sharedResources->interpolatedMutex);
            InterpolatedFrameCounters &frameCounters = ext.sharedResources->interpolatedFrames[ext.sharedResources->interpolatedFramesIndex];
            frameCounters.presented = frameCounters.count;
        }

        ext.sharedResources->interpolatedCondition.notify_all();
    }

    void PresentQueue::notifyPresentId(const Present &present) {
        {
            std::scoped_lock<std::mutex> cursorLock(presentIdMutex);
            presentId = present.presentId;
        }

        presentIdCondition.notify_all();
    }
    
    void PresentQueue::threadAdvanceBarrier() {
        std::scoped_lock<std::mutex> cursorLock(cursorMutex);
        barrierCursor = (barrierCursor + 1) % presents.size();
    }

    void PresentQueue::threadLoop() {
        Thread::setCurrentThreadName("RT64 Present");

        // Create the semaphore the acquire method will use.
        acquiredSemaphore = ext.device->createCommandSemaphore();

        // Create as many semaphores to signal as textures there are.
        while (drawSemaphores.size() < ext.swapChain->getTextureCount()) {
            drawSemaphores.emplace_back(ext.device->createCommandSemaphore());
        }

        // Since the swap chain might not need a resize right away, detect present wait.
        presentWaitEnabled = ext.device->getCapabilities().presentWait;

        int processCursor = -1;
        bool skipPresent = false;
        uint32_t displayTimingRate = UINT32_MAX;
        const bool displayTiming = ext.device->getCapabilities().displayTiming;
        bool swapChainValid = !ext.swapChain->needsResize();
        while (presentThreadRunning) {
            {
                std::unique_lock<std::mutex> cursorLock(cursorMutex);
                cursorCondition.wait(cursorLock, [&]() {
                    return (writeCursor != threadCursor) || !presentThreadRunning;
                });

                if (presentThreadRunning) {
                    processCursor = threadCursor;
                    threadCursor = (threadCursor + 1) % presents.size();
                    skipPresent = (writeCursor != threadCursor);
                }
            }

            if (processCursor >= 0) {
                std::unique_lock<std::mutex> threadLock(threadMutex);
                const bool needsResize = ext.swapChain->needsResize() || !swapChainValid;
                if (needsResize) {
                    ext.presentGraphicsWorker->commandList->begin();
                    ext.presentGraphicsWorker->commandList->end();
                    ext.presentGraphicsWorker->execute();
                    ext.presentGraphicsWorker->wait();
                    swapChainValid = ext.swapChain->resize();
                    swapChainFramebuffers.clear();

                    if (swapChainValid) {
                        ext.sharedResources->setSwapChainSize(ext.swapChain->getWidth(), ext.swapChain->getHeight());
                        
                        // Texture count could've changed after resize, so new semaphores are needed.
                        while (drawSemaphores.size() < ext.swapChain->getTextureCount()) {
                            drawSemaphores.emplace_back(ext.device->createCommandSemaphore());
                        }
                    }
                }

                if (needsResize || ext.appWindow->detectWindowMoved()) {
                    ext.appWindow->detectRefreshRate();
                    ext.sharedResources->setSwapChainRate(std::min(ext.appWindow->getRefreshRate(), displayTimingRate));
                }

                if (displayTiming) {
                    uint32_t newDisplayTimingRate = ext.swapChain->getRefreshRate();
                    if (newDisplayTimingRate == 0) {
                        newDisplayTimingRate = UINT32_MAX;
                    }

                    if (newDisplayTimingRate != displayTimingRate) {
                        ext.sharedResources->setSwapChainRate(std::min(ext.appWindow->getRefreshRate(), newDisplayTimingRate));
                        displayTimingRate = newDisplayTimingRate;
                    }
                }

                skipPresent = skipPresent || ext.swapChain->isEmpty();

                Present &present = presents[processCursor];
                ext.workloadQueue->waitForWorkloadId(present.workloadId);

                if (!presentThreadRunning) {
                    continue;
                }

                if (skipPresent) {
                    skipInterpolation();
                    notifyPresentId(present);
                }
                else {
                    threadPresent(present, swapChainValid);
                }

                if (!present.paused) {
                    if (!present.fbOperations.empty()) {
                        const std::scoped_lock lock(screenFbChangePoolMutex);
                        screenFbChangePool.release(present.fbOperations.front().writeChanges.id);
                        present.fbOperations.clear();
                    }

                    threadAdvanceBarrier();
                }

                processCursor = -1;
            }
        }

        // Transition the active swap chain render target out of the present state to avoid live references to the resource.
        uint32_t swapChainIndex = 0;
        if (!ext.swapChain->isEmpty() && ext.swapChain->acquireTexture(acquiredSemaphore.get(), &swapChainIndex)) {
            RenderTexture *swapChainTexture = ext.swapChain->getTexture(swapChainIndex);
            ext.presentGraphicsWorker->commandList->begin();
            ext.presentGraphicsWorker->commandList->barriers(RenderBarrierStage::NONE, RenderTextureBarrier(swapChainTexture, RenderTextureLayout::COLOR_WRITE));
            ext.presentGraphicsWorker->commandList->end();

            const RenderCommandList *commandList = ext.presentGraphicsWorker->commandList.get();
            RenderCommandSemaphore *waitSemaphore = acquiredSemaphore.get();
            ext.presentGraphicsWorker->commandQueue->executeCommandLists(&commandList, 1, &waitSemaphore, 1, nullptr, 0, ext.presentGraphicsWorker->commandFence.get());
            ext.presentGraphicsWorker->wait();
        }
    }
};
