//
// RT64
//

#include "rt64_render_target.h"

#include <algorithm>

#include "gbi/rt64_f3d.h"
#include "shared/rt64_fb_common.h"
#include "shared/rt64_render_target_copy.h"
#include "shared/rt64_texture_copy.h"

#include "rt64_raster_shader.h"
#include "hle/rt64_snap_diag.h"

#define PRINT_CONSTRUCTOR_DESTRUCTOR 0

namespace RT64 {
    // Pokemon Snap port, diagnostic (SNAP_OP_TRACE): one line per recorded
    // operation on a render target (hle/rt64_snap_diag.h, opTrace).
    static void snapOpTrace(RenderWorker *worker, const RenderTarget *target, const char *what, const char *detail) {
        if (!snapdiag::opTraceEnabled()) {
            return;
        }

        snapdiag::opTrace(worker->name.c_str(), "%s: target %08X %s %ux%u rev %llu tex %p: %s", what, target->addressForName,
            (target->type == Framebuffer::Type::Depth) ? "depth" : "color", target->width, target->height,
            (unsigned long long)target->textureRevision, (const void *)target->texture.get(), detail);
    }

    // RenderTarget
    
    const long RenderTarget::MaxDimension = 0x4000L;

    RenderTarget::RenderTarget(uint32_t addressForName, Framebuffer::Type type, const RenderMultisampling &multisampling, bool usesHDR) {
        this->addressForName = addressForName;
        this->type = type;
        this->multisampling = multisampling;
        this->usesHDR = usesHDR;

#if PRINT_CONSTRUCTOR_DESTRUCTOR
        fprintf(stdout, "RenderTarget(0x%p)\n", this);
#endif
    }

    RenderTarget::~RenderTarget() {
#if PRINT_CONSTRUCTOR_DESTRUCTOR
        fprintf(stdout, "~RenderTarget(0x%p)\n", this);
#endif
    }

    void RenderTarget::releaseTextures() {
        textureView.reset();
        resolvedTextureView.reset();
        texture.reset();
        resolvedTexture.reset();
        downsampledTexture.reset();
        dummyTexture.reset();
        textureFramebuffer.reset();
        resolveFramebuffer.reset();
        targetCopyDescSet.reset();
        textureCopyDescSet.reset();
        textureResolveDescSet.reset();
        filterDescSet.reset();
        fbWriteDescSet.reset();
    }

    bool RenderTarget::resize(RenderWorker *worker, uint32_t newWidth, uint32_t newHeight) {
        const bool insufficientSize = (width < newWidth) || (height < newHeight);
        if (insufficientSize) {
            // Pick the biggest width and height out of the current size and the new size.
            newWidth = std::max(width, newWidth);
            newHeight = std::max(height, newHeight);

            if (type == Framebuffer::Type::Depth) {
                setupDepth(worker, newWidth, newHeight);
            }
            else {
                setupColor(worker, newWidth, newHeight);
            }

            return true;
        }
        else {
            return false;
        }
    }

    void RenderTarget::setupColor(RenderWorker *worker, uint32_t width, uint32_t height) {
        assert(worker != nullptr);

        releaseTextures();

        this->width = width;
        this->height = height;

        downsampledTextureMultiplier = 0;
        format = colorBufferFormat(usesHDR);
        
        RenderClearValue clearValue = RenderClearValue::Color(RenderColor(), format);
        texture = worker->device->createTexture(RenderTextureDesc::ColorTarget(width, height, format, multisampling, &clearValue));
        textureView = texture->createTextureView(RenderTextureViewDesc::Texture2D(format));
        texture->setName("Render Target Color #" + std::to_string(addressForName));
        textureRevision++;
        snapFreshMemory = true;
        snapOpTrace(worker, this, "setupColor", "created");

        if (multisampling.sampleCount > 1) {
            resolvedTexture = worker->device->createTexture(RenderTextureDesc::ColorTarget(width, height, format, RenderMultisampling(), &clearValue));
            resolvedTextureView = resolvedTexture->createTextureView(RenderTextureViewDesc::Texture2D(format));
            resolvedTexture->setName("Render Target Color Resolved #" + std::to_string(addressForName));
        }
    }

    void RenderTarget::setupDepth(RenderWorker *worker, uint32_t width, uint32_t height) {
        assert(worker != nullptr);

        releaseTextures();

        this->width = width;
        this->height = height;

        downsampledTextureMultiplier = 0;
        format = depthBufferFormat();
        
        RenderClearValue clearValue = RenderClearValue::Depth(RenderDepth(), RenderFormat::D32_FLOAT);
        texture = worker->device->createTexture(RenderTextureDesc::DepthTarget(width, height, format, multisampling, &clearValue));
        textureView = texture->createTextureView(RenderTextureViewDesc::Texture2D(format));
        texture->setName("Render Target Depth #" + std::to_string(addressForName));
        textureRevision++;
        snapFreshMemory = true;
        snapOpTrace(worker, this, "setupDepth", "created");
    }

    void RenderTarget::setupDummy(RenderWorker *worker) {
        assert(worker != nullptr);
        
        if (dummyTexture == nullptr) {
            dummyTexture = worker->device->createTexture(RenderTextureDesc::ColorTarget(width, height, colorBufferFormat(usesHDR), multisampling));
            dummyTexture->setName("Render Target Dummy");
        }
    }

    void RenderTarget::setupColorFramebuffer(RenderWorker *worker) {
        assert(worker != nullptr);

        if (textureFramebuffer == nullptr) {
            const RenderTexture *colorTexture = texture.get();
            textureFramebuffer = worker->device->createFramebuffer(RenderFramebufferDesc(&colorTexture, 1));
        }

        // Pokemon Snap port: a new texture's memory is whatever the driver
        // handed back. Direct3D 12 on Windows gives zeroed pages; Vulkan on a
        // Steam Deck (RADV) gives a previous owner's contents. The game's
        // framebuffer is read into the target from RAM, but only over the
        // framebuffer's own area: the margins the widescreen expansion adds
        // are written by nothing until the picture reaches them, and a scene
        // that scissors itself narrower than the frame -- a course's opening
        // cinematic, its dead border included -- left them showing that
        // stale memory, half black and half an earlier frame, on a Deck in
        // Widescreen (the port's author, 2026-09-06 and 2026-09-12). This is
        // the first recorded use of a fresh target, ahead of the RAM read,
        // so the clear lands under it: black on every platform, as Windows
        // already showed.
        if (snapFreshMemory) {
            snapFreshMemory = false;
            snapOpTrace(worker, this, "freshClearColor", "");
            worker->commandList->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(texture.get(), RenderTextureLayout::COLOR_WRITE));
            worker->commandList->setFramebuffer(textureFramebuffer.get());
            worker->commandList->clearColor();
            markForResolve();
        }
    }

    void RenderTarget::setupResolveFramebuffer(RenderWorker *worker) {
        assert(worker != nullptr);
        assert(resolvedTexture != nullptr);

        if (resolveFramebuffer == nullptr) {
            const RenderTexture *colorTexture = resolvedTexture.get();
            resolveFramebuffer = worker->device->createFramebuffer(RenderFramebufferDesc(&colorTexture, 1));
        }
    }

    void RenderTarget::setupDepthFramebuffer(RenderWorker *worker) {
        assert(worker != nullptr);

        setupDummy(worker);

        if (textureFramebuffer == nullptr) {
            const RenderTexture *colorTexture = dummyTexture.get();
            textureFramebuffer = worker->device->createFramebuffer(RenderFramebufferDesc(&colorTexture, 1, texture.get()));
        }

        // Pokemon Snap port: the same first-use clear as the colour target's
        // (setupColorFramebuffer): stale depth in the expansion's margins
        // would reject geometry there at random.
        if (snapFreshMemory) {
            snapFreshMemory = false;
            snapOpTrace(worker, this, "freshClearDepth", "");
            RenderTextureBarrier clearBarriers[] = {
                RenderTextureBarrier(texture.get(), RenderTextureLayout::DEPTH_WRITE),
                RenderTextureBarrier(dummyTexture.get(), RenderTextureLayout::COLOR_WRITE)
            };
            worker->commandList->barriers(RenderBarrierStage::GRAPHICS, clearBarriers, uint32_t(std::size(clearBarriers)));
            worker->commandList->setFramebuffer(textureFramebuffer.get());
            worker->commandList->clearDepth();
        }
    }
    
    void RenderTarget::copyFromTarget(RenderWorker *worker, RenderTarget *src, uint32_t x, uint32_t y, uint32_t width, uint32_t height, const ShaderLibrary *shaderLibrary) {
        assert(worker != nullptr);
        assert(src != nullptr);
        assert(format != src->format);
        if (snapdiag::opTraceEnabled()) {
            char detail[200];
            snprintf(detail, sizeof(detail), "from %08X %s %ux%u rev %llu tex %p samples %u, rect (%u,%u %ux%u)", src->addressForName,
                (src->type == Framebuffer::Type::Depth) ? "depth" : "color", src->width, src->height, (unsigned long long)src->textureRevision,
                (const void *)src->texture.get(), src->multisampling.sampleCount, x, y, width, height);
            snapOpTrace(worker, this, "copyFromTarget", detail);
        }

        // Select shader based on the formats.
        RenderTextureLayout requiredTextureLayout = RenderTextureLayout::UNKNOWN;
        const ShaderRecord *shaderRecord = nullptr;
        bool useDummyTexture = false;
        const bool srcUsesMSAA = (src->multisampling.sampleCount > 1);
        if ((format == colorBufferFormat(usesHDR)) && (src->format == depthBufferFormat())) {
            shaderRecord = srcUsesMSAA ? &shaderLibrary->rtCopyDepthToColorMS : &shaderLibrary->rtCopyDepthToColor;
            requiredTextureLayout = RenderTextureLayout::COLOR_WRITE;
            setupColorFramebuffer(worker);
        }
        else if ((format == depthBufferFormat()) && (src->format == colorBufferFormat(usesHDR))) {
            useDummyTexture = true;
            shaderRecord = srcUsesMSAA ? &shaderLibrary->rtCopyColorToDepthMS : &shaderLibrary->rtCopyColorToDepth;
            requiredTextureLayout = RenderTextureLayout::DEPTH_WRITE;
            setupDepthFramebuffer(worker);
        }
        else {
            assert(false && "Unsupported render target copy.");
        }

        if (src->targetCopyDescSet == nullptr) {
            src->targetCopyDescSet = std::make_unique<RenderTargetCopyDescriptorSet>(worker->device);
            src->targetCopyDescSet->setTexture(src->targetCopyDescSet->gInput, src->texture.get(), RenderTextureLayout::SHADER_READ, src->textureView.get());
        }

        thread_local std::vector<RenderTextureBarrier> framebufferBarriers;
        framebufferBarriers.clear();
        framebufferBarriers.emplace_back(RenderTextureBarrier(texture.get(), requiredTextureLayout));
        framebufferBarriers.emplace_back(RenderTextureBarrier(src->texture.get(), RenderTextureLayout::SHADER_READ));
        if (useDummyTexture) {
            framebufferBarriers.emplace_back(RenderTextureBarrier(dummyTexture.get(), RenderTextureLayout::COLOR_WRITE));
        }

        worker->commandList->barriers(RenderBarrierStage::GRAPHICS, framebufferBarriers);
        worker->commandList->setFramebuffer(textureFramebuffer.get());

        interop::RenderTargetCopyCB copyCB;
        copyCB.usesHDR = usesHDR;

        // Record the drawing command.
        RenderViewport targetViewport = RenderViewport(float(x), float(y), float(width), float(height));
        RenderRect targetRect(x, y, x + width, y + height);
        worker->commandList->setViewports(targetViewport);
        worker->commandList->setScissors(targetRect);
        worker->commandList->setPipeline(shaderRecord->pipeline.get());
        worker->commandList->setGraphicsPipelineLayout(shaderRecord->pipelineLayout.get());
        worker->commandList->setGraphicsDescriptorSet(src->targetCopyDescSet->get(), 0);
        worker->commandList->setGraphicsPushConstants(0, &copyCB);
        worker->commandList->setVertexBuffers(0, nullptr, 0, nullptr);
        worker->commandList->drawInstanced(3, 1, 0, 0);

        markForResolve();
    }

    void RenderTarget::resolveFromTarget(RenderWorker *worker, RenderTarget *src, const ShaderLibrary *shaderLibrary) {
        assert(!usesResolve() && "The target must not be an MSAA target to allow resolving from other targets.");

        const bool hwResolve = shaderLibrary->usesHardwareResolve && worker->device->getCapabilities().resolveRegion;
        if (snapdiag::opTraceEnabled()) {
            char detail[160];
            snprintf(detail, sizeof(detail), "from %08X %ux%u rev %llu tex %p samples %u, hw %d", src->addressForName, src->width, src->height,
                (unsigned long long)src->textureRevision, (const void *)src->texture.get(), src->multisampling.sampleCount, hwResolve ? 1 : 0);
            snapOpTrace(worker, this, "resolveFromTarget", detail);
        }
        RenderTextureBarrier resolveBarriers[] = {
            RenderTextureBarrier(src->texture.get(), hwResolve ? RenderTextureLayout::RESOLVE_SOURCE : RenderTextureLayout::SHADER_READ),
            RenderTextureBarrier(texture.get(), hwResolve ? RenderTextureLayout::RESOLVE_DEST : RenderTextureLayout::COLOR_WRITE)
        };
        
        const RenderRect srcRect(0, 0, std::min(width, src->width), std::min(height, src->height));
        worker->commandList->barriers(hwResolve ? RenderBarrierStage::COPY : RenderBarrierStage::GRAPHICS, resolveBarriers, uint32_t(std::size(resolveBarriers)));

        if (hwResolve) {
            worker->commandList->resolveTextureRegion(texture.get(), 0, 0, src->texture.get(), &srcRect);
        }
        else {
            if (src->textureResolveDescSet == nullptr) {
                src->textureResolveDescSet = std::make_unique<TextureCopyDescriptorSet>(worker->device);
                src->textureResolveDescSet->setTexture(src->textureResolveDescSet->gInput, src->texture.get(), RenderTextureLayout::SHADER_READ, src->textureView.get());
            }

            recordRasterResolve(worker, src->textureResolveDescSet.get(), srcRect.left, srcRect.top, srcRect.right - srcRect.left, srcRect.bottom - srcRect.top, shaderLibrary);
        }

        // Copy scaling attributes from source.
        resolutionScale = src->resolutionScale;
        downsampleMultiplier = src->downsampleMultiplier;
        misalignX = src->misalignX;
        invMisalignX = src->invMisalignX;
    }

    // Pokemon Snap port: the whole picture of one single-sampled colour target
    // drawn into another through the rasterizer, with the texture copy shader
    // the tile copies use, instead of being recorded as a transfer. RT64
    // keeps transfer commands away from its render targets on purpose
    // (hle/rt64_framebuffer_manager.cpp, the copy regions: "does not behave
    // consistently enough across hardware"), and the cut-transit hold was
    // the port's one transfer into such a target (interpolated target 0,
    // which resolves are written to). Made while the Deck's top-left chunk
    // was being hunted, and NOT that fault's cure -- the chunk was the
    // driver's NGG culling (src/main.cpp, snap_radv_debug_nonggc) -- but
    // kept for the reason RT64 gives: every write to the target then goes
    // through the same colour path. Verified to show the same picture by
    // presented-frame capture of the intro's holds on Windows (Vulkan,
    // anti-aliasing 4x at 90 Hz, 2026-09-12).
    void RenderTarget::snapCopyFromTargetRaster(RenderWorker *worker, RenderTarget *src, const ShaderLibrary *shaderLibrary) {
        assert(worker != nullptr);
        assert(src != nullptr);
        assert(format == src->format);
        assert(multisampling.sampleCount == 1);
        assert(src->multisampling.sampleCount == 1);

        if (src->textureCopyDescSet == nullptr) {
            src->textureCopyDescSet = std::make_unique<TextureCopyDescriptorSet>(worker->device);
            src->textureCopyDescSet->setTexture(src->textureCopyDescSet->gInput, src->getResolvedTexture(), RenderTextureLayout::SHADER_READ, src->getResolvedTextureView());
        }

        setupColorFramebuffer(worker);

        if (snapdiag::opTraceEnabled()) {
            char detail[160];
            snprintf(detail, sizeof(detail), "from %08X %ux%u rev %llu tex %p", src->addressForName, src->width, src->height,
                (unsigned long long)src->textureRevision, (const void *)src->texture.get());
            snapOpTrace(worker, this, "copyFromTargetRaster", detail);
        }

        RenderTextureBarrier copyBarriers[] = {
            RenderTextureBarrier(src->getResolvedTexture(), RenderTextureLayout::SHADER_READ),
            RenderTextureBarrier(texture.get(), RenderTextureLayout::COLOR_WRITE)
        };

        worker->commandList->barriers(RenderBarrierStage::GRAPHICS, copyBarriers, uint32_t(std::size(copyBarriers)));
        worker->commandList->setFramebuffer(textureFramebuffer.get());

        const uint32_t copyWidth = std::min(width, src->width);
        const uint32_t copyHeight = std::min(height, src->height);
        interop::TextureCopyCB copyCB;
        copyCB.uvScroll = { 0.0f, 0.0f };
        copyCB.uvScale = { float(copyWidth), float(copyHeight) };
        copyCB.boxSize = { 1, 1 };

        const ShaderRecord &textureCopy = shaderLibrary->textureCopy;
        worker->commandList->setPipeline(textureCopy.pipeline.get());
        worker->commandList->setGraphicsPipelineLayout(textureCopy.pipelineLayout.get());
        worker->commandList->setVertexBuffers(0, nullptr, 0, nullptr);
        worker->commandList->setViewports(RenderViewport(0.0f, 0.0f, float(copyWidth), float(copyHeight)));
        worker->commandList->setScissors(RenderRect(0, 0, int32_t(copyWidth), int32_t(copyHeight)));
        worker->commandList->setGraphicsDescriptorSet(src->textureCopyDescSet->get(), 0);
        worker->commandList->setGraphicsPushConstants(0, &copyCB);
        worker->commandList->drawInstanced(3, 1, 0, 0);

        markForResolve();
    }

    void RenderTarget::copyFromChanges(RenderWorker *worker, const FramebufferChange &fbChange, uint32_t fbWidth, uint32_t fbHeight, uint32_t rowStart, const ShaderLibrary *shaderLibrary) {
        assert(worker != nullptr);
        assert(fbChange.used);

        // Select shader based on the formats.
        RenderTextureLayout requiredTextureLayout = RenderTextureLayout::UNKNOWN;
        const ShaderRecord *shaderRecord = nullptr;
        bool useDummyTexture = false;
        if (format == colorBufferFormat(usesHDR)) {
            shaderRecord = &shaderLibrary->fbChangesDrawColor;
            requiredTextureLayout = RenderTextureLayout::COLOR_WRITE;
            setupColorFramebuffer(worker);
        }
        else if (format == depthBufferFormat()) {
            useDummyTexture = true;
            shaderRecord = &shaderLibrary->fbChangesDrawDepth;
            requiredTextureLayout = RenderTextureLayout::DEPTH_WRITE;
            setupDepthFramebuffer(worker);
        }
        else {
            assert(false && "Unsupported buffer format.");
        }

        thread_local std::vector<RenderTextureBarrier> framebufferBarriers;
        framebufferBarriers.clear();
        framebufferBarriers.emplace_back(RenderTextureBarrier(texture.get(), requiredTextureLayout));
        if (fbChange.pixelTexture != nullptr) { framebufferBarriers.emplace_back(RenderTextureBarrier(fbChange.pixelTexture.get(), RenderTextureLayout::SHADER_READ)); }
        if (fbChange.booleanTexture != nullptr) { framebufferBarriers.emplace_back(RenderTextureBarrier(fbChange.booleanTexture.get(), RenderTextureLayout::SHADER_READ)); }
        if (useDummyTexture) { framebufferBarriers.emplace_back(RenderTextureBarrier(dummyTexture.get(), RenderTextureLayout::COLOR_WRITE)); }

        worker->commandList->barriers(RenderBarrierStage::GRAPHICS, framebufferBarriers);
        worker->commandList->setFramebuffer(textureFramebuffer.get());

        struct FbChangesDrawCommonCB {
            uint32_t Resolution[2];
        };

        FbChangesDrawCommonCB commonCB;
        commonCB.Resolution[0] = fbWidth;
        commonCB.Resolution[1] = fbHeight;

        // Adjust the viewport and scissor to the center.
        // TODO: Should it query from the FB pair somehow if it's an FB pair that is supposed to have aspect ratio adjustment?
        float targetWidth, targetHeight, targetLeft, targetTop;
        const bool pillarBox = resolutionScale[0] > resolutionScale[1];
        if (pillarBox) {
            targetWidth = fbWidth * resolutionScale.y;
            targetHeight = fbHeight * resolutionScale.y;
            targetLeft = ((fbWidth * resolutionScale.x) / 2.0f) - (targetWidth / 2.0f);
            targetTop = rowStart * resolutionScale.y;
        }
        else {
            targetWidth = fbWidth * resolutionScale.x;
            targetHeight = fbHeight * resolutionScale.x;
            targetLeft = 0.0f;
            targetTop = rowStart * resolutionScale.y + ((fbHeight * resolutionScale.y) / 2.0f) - (targetHeight / 2.0f);
        }
        
        if (snapdiag::opTraceEnabled()) {
            char detail[200];
            snprintf(detail, sizeof(detail), "fb %ux%u rows from %u, scale %.3fx%.3f pillar %d, drawn at (%.1f,%.1f %.1fx%.1f), change %ux%u",
                fbWidth, fbHeight, rowStart, float(resolutionScale.x), float(resolutionScale.y), pillarBox ? 1 : 0,
                targetLeft, targetTop, targetWidth, targetHeight, fbChange.width, fbChange.height);
            snapOpTrace(worker, this, "copyFromChanges", detail);
        }

        // Record the drawing command.
        long scissorLeft = long(floor(targetLeft));
        long scissorTop = long(floor(targetTop));
        long scissorRight = long(ceil(targetLeft + targetWidth));
        long scissorBottom = long(ceil(targetTop + targetHeight));
        RenderViewport targetViewport = RenderViewport(targetLeft, targetTop, targetWidth, targetHeight);
        RenderRect targetRect(scissorLeft, scissorTop, scissorRight, scissorBottom);
        worker->commandList->setViewports(targetViewport);
        worker->commandList->setScissors(targetRect);
        worker->commandList->setPipeline(shaderRecord->pipeline.get());
        worker->commandList->setGraphicsPipelineLayout(shaderRecord->pipelineLayout.get());
        worker->commandList->setGraphicsPushConstants(0, &commonCB);
        worker->commandList->setGraphicsDescriptorSet(fbChange.drawDescSet->get(), 0);
        worker->commandList->setVertexBuffers(0, nullptr, 0, nullptr);
        worker->commandList->drawInstanced(3, 1, 0, 0);

        markForResolve();
    }

    void RenderTarget::clearColorTarget(RenderWorker *worker) {
        assert(worker != nullptr);
        snapOpTrace(worker, this, "clearColorTarget", "");

        setupColorFramebuffer(worker);

        worker->commandList->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(texture.get(), RenderTextureLayout::COLOR_WRITE));
        worker->commandList->setFramebuffer(textureFramebuffer.get());
        worker->commandList->clearColor();

        markForResolve();
    }

    void RenderTarget::clearDepthTarget(RenderWorker *worker) {
        assert(worker != nullptr);
        snapOpTrace(worker, this, "clearDepthTarget", "");

        setupDepthFramebuffer(worker);
        
        RenderTextureBarrier clearBarriers[] = {
            RenderTextureBarrier(texture.get(), RenderTextureLayout::DEPTH_WRITE),
            RenderTextureBarrier(dummyTexture.get(), RenderTextureLayout::COLOR_WRITE)
        };

        worker->commandList->barriers(RenderBarrierStage::GRAPHICS, clearBarriers, uint32_t(std::size(clearBarriers)));
        worker->commandList->setFramebuffer(textureFramebuffer.get());
        worker->commandList->clearDepth();
        
        markForResolve();
    }

    void RenderTarget::downsampleTarget(RenderWorker *worker, const ShaderLibrary *shaderLibrary) {
        assert(worker != nullptr);

        resolveTarget(worker, shaderLibrary);

        struct BoxFilterCB {
            interop::int2 Resolution;
            interop::int2 ResolutionScale;
            interop::int2 Misalignment;
        };

        if (downsampledTextureMultiplier != downsampleMultiplier) {
            downsampledTexture.reset(nullptr);
            downsampledTextureMultiplier = 0;
        }

        uint32_t scaledWidth = std::max(width / downsampleMultiplier, 1U);
        uint32_t scaledHeight = std::max(height / downsampleMultiplier, 1U);
        if (downsampledTexture == nullptr) {
            downsampledTexture = worker->device->createTexture(RenderTextureDesc::Texture2D(scaledWidth, scaledHeight, 1, colorBufferFormat(usesHDR), RenderTextureFlag::STORAGE | RenderTextureFlag::UNORDERED_ACCESS));
            downsampledTexture->setName("Render Target Downsampled");
            downsampledTextureMultiplier = downsampleMultiplier;
        }

        if (filterDescSet == nullptr) {
            filterDescSet = std::make_unique<BoxFilterDescriptorSet>(worker->device);
        }

        filterDescSet->setTexture(filterDescSet->gInput, getResolvedTexture(), RenderTextureLayout::SHADER_READ);
        filterDescSet->setTexture(filterDescSet->gOutput, downsampledTexture.get(), RenderTextureLayout::GENERAL);

        BoxFilterCB boxFilterCB;
        boxFilterCB.Resolution[0] = width;
        boxFilterCB.Resolution[1] = height;
        boxFilterCB.ResolutionScale[0] = downsampleMultiplier;
        boxFilterCB.ResolutionScale[1] = downsampleMultiplier;
        boxFilterCB.Misalignment[0] = invMisalignX;
        boxFilterCB.Misalignment[1] = 0;
        
        RenderTextureBarrier filterBarriers[] = {
            RenderTextureBarrier(getResolvedTexture(), RenderTextureLayout::SHADER_READ),
            RenderTextureBarrier(downsampledTexture.get(), RenderTextureLayout::GENERAL)
        };

        // Execute the compute shader for downsampling the image.
        const uint32_t BlockSize = 8;
        const uint32_t dispatchX = (scaledWidth + BlockSize - 1) / BlockSize;
        const uint32_t dispatchY = (scaledHeight + BlockSize - 1) / BlockSize;
        worker->commandList->barriers(RenderBarrierStage::COMPUTE, filterBarriers, uint32_t(std::size(filterBarriers)));
        worker->commandList->setPipeline(shaderLibrary->boxFilter.pipeline.get());
        worker->commandList->setComputePipelineLayout(shaderLibrary->boxFilter.pipelineLayout.get());
        worker->commandList->setComputePushConstants(0, &boxFilterCB);
        worker->commandList->setComputeDescriptorSet(filterDescSet->get(), 0);
        worker->commandList->dispatch(dispatchX, dispatchY, 1);
    }

    void RenderTarget::resolveTarget(RenderWorker *worker, const ShaderLibrary *shaderLibrary) {
        if (!resolvedTextureDirty || !usesResolve()) {
            return;
        }

        const bool hwResolve = shaderLibrary->usesHardwareResolve;
        snapOpTrace(worker, this, "resolveTarget", hwResolve ? "hw" : "raster");
        RenderTextureBarrier resolveBarriers[] = {
            RenderTextureBarrier(texture.get(), hwResolve ? RenderTextureLayout::RESOLVE_SOURCE : RenderTextureLayout::SHADER_READ),
            RenderTextureBarrier(resolvedTexture.get(), hwResolve ? RenderTextureLayout::RESOLVE_DEST : RenderTextureLayout::COLOR_WRITE)
        };

        worker->commandList->barriers(hwResolve ? RenderBarrierStage::COPY : RenderBarrierStage::GRAPHICS, resolveBarriers, uint32_t(std::size(resolveBarriers)));
        
        if (hwResolve) {
            worker->commandList->resolveTexture(resolvedTexture.get(), texture.get());
        }
        else {
            if (textureResolveDescSet == nullptr) {
                textureResolveDescSet = std::make_unique<TextureCopyDescriptorSet>(worker->device);
                textureResolveDescSet->setTexture(textureResolveDescSet->gInput, texture.get(), RenderTextureLayout::SHADER_READ, textureView.get());
            }

            recordRasterResolve(worker, textureResolveDescSet.get(), 0, 0, width, height, shaderLibrary);
        }

        resolvedTextureDirty = false;
    }

    void RenderTarget::recordRasterResolve(RenderWorker *worker, const TextureCopyDescriptorSet *srcDescriptorSet, uint32_t x, uint32_t y, uint32_t width, uint32_t height, const ShaderLibrary *shaderLibrary) {
        assert(format != depthBufferFormat() && "Rasterization path should not be used for depth targets.");

        if (resolvedTexture != nullptr) {
            setupResolveFramebuffer(worker);
            worker->commandList->setFramebuffer(resolveFramebuffer.get());
        }
        else {
            setupColorFramebuffer(worker);
            worker->commandList->setFramebuffer(textureFramebuffer.get());
        }

        interop::TextureCopyCB textureCopyCB;
        textureCopyCB.uvScroll.x = float(x);
        textureCopyCB.uvScroll.y = float(y);
        textureCopyCB.uvScale.x = float(width);
        textureCopyCB.uvScale.y = float(height);
        textureCopyCB.boxSize = { 1, 1 };

        const ShaderRecord &textureResolve = shaderLibrary->textureResolve;
        worker->commandList->setPipeline(textureResolve.pipeline.get());
        worker->commandList->setGraphicsPipelineLayout(textureResolve.pipelineLayout.get());
        worker->commandList->setVertexBuffers(0, nullptr, 0, nullptr);
        worker->commandList->setViewports(RenderViewport(float(x), float(y), float(width), float(height)));
        worker->commandList->setScissors(RenderRect(x, y, x + width, y + height));
        worker->commandList->setGraphicsDescriptorSet(srcDescriptorSet->get(), 0);
        worker->commandList->setGraphicsPushConstants(0, &textureCopyCB);
        worker->commandList->drawInstanced(3, 1, 0, 0);
    }

    void RenderTarget::markForResolve() {
        resolvedTextureDirty = true;
    }

    bool RenderTarget::usesResolve() const {
        return resolvedTexture != nullptr;
    }

    RenderTexture *RenderTarget::getResolvedTexture() const {
        return usesResolve() ? resolvedTexture.get() : texture.get();
    }
    
    RenderTextureView *RenderTarget::getResolvedTextureView() const {
        return usesResolve() ? resolvedTextureView.get() : textureView.get();
    }

    bool RenderTarget::isEmpty() const {
        return texture == nullptr;
    }

    void RenderTarget::computeScaledSize(uint32_t nativeWidth, uint32_t nativeHeight, hlslpp::float2 resolutionScale, uint32_t &scaledWidth, uint32_t &scaledHeight, uint32_t &misalignmentX) {
        const long nativeColorWidthClamped = std::clamp(lround(nativeWidth * resolutionScale.y), 1L, RenderTarget::MaxDimension);
        const long expandedColorWidthClamped = std::clamp(lround(nativeWidth * resolutionScale.x), 1L, RenderTarget::MaxDimension);
        const long colorHeightClamped = std::clamp(lround(nativeHeight * resolutionScale.y), 1L, RenderTarget::MaxDimension);
        scaledWidth = uint32_t(expandedColorWidthClamped);
        scaledHeight = uint32_t(colorHeightClamped);

        const long expandedPixels = std::labs(long(scaledWidth) - nativeColorWidthClamped) / 2;
        const long nativeAlignment = std::max(lround(resolutionScale.y), 1L);
        misalignmentX = (nativeAlignment - (expandedPixels % nativeAlignment)) % nativeAlignment;
    }

    hlslpp::float2 RenderTarget::computeFixedResolutionScale(uint32_t nativeWidth, hlslpp::float2 resolutionScale) {
        long expandedColorWidthClamped = std::clamp(lround(nativeWidth * resolutionScale.x), 1L, RenderTarget::MaxDimension);

        // Alter the resolution scale so it outputs an even resolution number.
        expandedColorWidthClamped += expandedColorWidthClamped & 0x1;
        resolutionScale.x = float(expandedColorWidthClamped) / float(nativeWidth);
        return resolutionScale;
    }

    RenderFormat RenderTarget::colorBufferFormat(bool usesHDR) {
        return usesHDR ? RenderFormat::R16G16B16A16_UNORM : RenderFormat::R8G8B8A8_UNORM;
    }

    RenderFormat RenderTarget::depthBufferFormat() {
        return RenderFormat::D32_FLOAT;
    }
};