//
// RT64
//

#include "rt64_projection_processor.h"

#include "common/rt64_math.h"
#include "hle/rt64_workload_queue.h"

namespace RT64 {
    inline void adjustProjectionMatrix(interop::float4x4 &matrix, const float aspectRatioScale) {
        matrix[0][0] *= aspectRatioScale;
        matrix[1][0] *= aspectRatioScale;
        matrix[2][0] *= aspectRatioScale;
        matrix[3][0] *= aspectRatioScale;
    }
    
    // ProjectionProcessor

    ProjectionProcessor::ProjectionProcessor() { }

    ProjectionProcessor::~ProjectionProcessor() {
        bufferUploader.reset(nullptr);
    }

    void ProjectionProcessor::setup(RenderWorker *worker) {
        bufferUploader = std::make_unique<BufferUploader>(worker->device);
    }

    void ProjectionProcessor::process(const ProcessParams &p) {
        for (uint32_t w : p.curFrame->workloads) {
            Workload &workload = p.workloadQueue->workloads[w];
            DrawData &drawData = workload.drawData;

            // Copy the data.
            drawData.modViewTransforms = drawData.viewTransforms;
            drawData.modProjTransforms = drawData.projTransforms;
            drawData.modViewProjTransforms = drawData.viewProjTransforms;
            drawData.prevViewTransforms = drawData.viewTransforms;
            drawData.prevProjTransforms = drawData.projTransforms;
            drawData.prevViewProjTransforms = drawData.viewProjTransforms;
            drawData.modRspViewports = drawData.rspViewports;
        }

        for (size_t s = 0; s < p.curFrame->perspectiveScenes.size(); s++) {
            processScene(p, p.curFrame->perspectiveScenes[s], s);
        }

        for (size_t s = 0; s < p.curFrame->orthographicScenes.size(); s++) {
            processScene(p, p.curFrame->orthographicScenes[s], s);
        }
    }

    void ProjectionProcessor::processScene(const ProcessParams &p, const GameScene &scene, size_t sceneIndex) {
        for (size_t i = 0; i < scene.projections.size(); i++) {
            const GameIndices::Projection &sceneProj = scene.projections[i];
            Workload &workload = p.workloadQueue->workloads[sceneProj.workloadIndex];
            DrawData &drawData = workload.drawData;
            FramebufferPair &fbPair = workload.fbPairs[sceneProj.fbPairIndex];
            Projection &proj = fbPair.projections[sceneProj.projectionIndex];
            const uint16_t viewportOrigin = drawData.viewportOrigins[proj.transformsIndex];
            assert(proj.transformsIndex > 0);

            // Recomputed below for the sub-frame being rendered; stale truth
            // from the previous sub-frame must not outlive it.
            proj.snapScissorBlended = false;

            // Skip projections that didn't actually draw anything.
            if (proj.scissorRect.isNull()) {
                continue;
            }

            // Check the current mapping for the projection.
            const interop::float4x4 *prevProjMatrix = nullptr;
            const interop::float4x4 *prevViewMatrix = nullptr;
            interop::float4x4 rebasedPrevView;
            const RigidBody *rigidBody = nullptr;
            const Workload *prevWorkloadPtr = nullptr;
            uint32_t prevTransformIndex = 0;
            const GameFrameMap::WorkloadMap &workloadMap = p.curFrame->frameMap.workloads[sceneProj.workloadIndex];
            if ((p.prevFrame != nullptr) && workloadMap.mapped && !workload.debuggerCamera.enabled &&
                p.workloadQueue->snapInterpolateCamera.load(std::memory_order_relaxed)) {
                const GameFrameMap::ViewProjectionMap &viewProjMap = workloadMap.viewProjections[proj.transformsIndex];
                if (viewProjMap.mapped) {
                    const Workload &prevWorkload = p.workloadQueue->workloads[workloadMap.prevWorkloadIndex];
                    prevViewMatrix = &prevWorkload.drawData.viewTransforms[viewProjMap.prevTransformIndex];

                    // Matching read the previous camera in this frame's origin,
                    // so the interpolation has to as well or the two disagree by
                    // the whole shift.
                    if (viewProjMap.snapRebasedPrev) {
                        rebasedPrevView = hlslpp::mul(matrixTranslation(-p.curFrame->snapOriginDelta), *prevViewMatrix);
                        prevViewMatrix = &rebasedPrevView;
                    }
                    prevProjMatrix = &prevWorkload.drawData.projTransforms[viewProjMap.prevTransformIndex];
                    rigidBody = &viewProjMap.rigidBody;
                    prevWorkloadPtr = &prevWorkload;
                    prevTransformIndex = viewProjMap.prevTransformIndex;
                }
            }

            const uint32_t curProjGroupIndex = workload.drawData.viewProjTransformGroups[proj.transformsIndex];
            const TransformGroup &curProjGroup = workload.drawData.transformGroups[curProjGroupIndex];
            bool adjustAspectRatio = (curProjGroup.aspectMode == G_EX_ASPECT_ADJUST);
            if (curProjGroup.aspectMode == G_EX_ASPECT_AUTO) {
                FixedRect intersectionRect = proj.scissorRect;
                if (proj.usesViewport()) {
                    const interop::RSPViewport &viewport = drawData.rspViewports[proj.transformsIndex];
                    const int16_t *viewportClipRatios = &drawData.viewportClipRatios[proj.transformsIndex * 4];
                    intersectionRect = intersectionRect.intersection(viewport.rect(viewportClipRatios));
                }

                if (!intersectionRect.isEmpty()) {
                    bool coversWholeWidth = (intersectionRect.ulx <= fbPair.scissorRect.ulx) && (intersectionRect.lrx >= fbPair.scissorRect.lrx);
                    bool horizontalRatio = (intersectionRect.width(true, true) > intersectionRect.height(true, true));
                    adjustAspectRatio = (viewportOrigin == G_EX_ORIGIN_NONE) && coversWholeWidth && horizontalRatio;
                }
            }
 
            float projRatioScale = adjustAspectRatio ? (1.0f / p.aspectRatioScale) : 1.0f;
            interop::float4x4 &viewMatrix = drawData.modViewTransforms[proj.transformsIndex];
            interop::float4x4 &projMatrix = drawData.modProjTransforms[proj.transformsIndex];
            interop::float4x4 &viewProjMatrix = drawData.modViewProjTransforms[proj.transformsIndex];
            viewMatrix = drawData.viewTransforms[proj.transformsIndex];
            projMatrix = drawData.projTransforms[proj.transformsIndex];
            viewProjMatrix = drawData.viewProjTransforms[proj.transformsIndex];

            // Debugger camera.
            if (workload.debuggerCamera.enabled && (proj.type == Projection::Type::Perspective) && (workload.debuggerCamera.sceneIndex == sceneIndex)) {
                viewMatrix = workload.debuggerCamera.viewMatrix;
                projMatrix = workload.debuggerCamera.projMatrix;
            }

            adjustProjectionMatrix(projMatrix, projRatioScale);

            interop::float4x4 &prevViewTransform = drawData.prevViewTransforms[proj.transformsIndex];
            interop::float4x4 &prevProjTransform = drawData.prevProjTransforms[proj.transformsIndex];
            if ((prevProjMatrix != nullptr) && (prevViewMatrix != nullptr) && (rigidBody != nullptr)) {
                const interop::float4x4 curViewTransform = viewMatrix;
                const interop::float4x4 curProjTransform = projMatrix;
                interop::float4x4 adjustedPrevProj = *prevProjMatrix;
                adjustProjectionMatrix(adjustedPrevProj, projRatioScale);
                viewMatrix = rigidBody->lerp(p.curFrameWeight, *prevViewMatrix, curViewTransform, true);
                prevViewTransform = rigidBody->lerp(p.prevFrameWeight, *prevViewMatrix, curViewTransform, true);

                // We only interpolate the projection if the view matrix has been interpolated.
                const bool interpolateProjection = rigidBody->lerpTranslation || rigidBody->lerpRotation;
                if (interpolateProjection) {
                    projMatrix = lerpMatrix(adjustedPrevProj, curProjTransform, p.curFrameWeight);
                    prevProjTransform = lerpMatrix(adjustedPrevProj, curProjTransform, p.prevFrameWeight);
                }
                else {
                    projMatrix = curProjTransform;
                    prevProjTransform = curProjTransform;
                }

                // Pokemon Snap port: the game scales its scene into an
                // animated viewport inset -- the photo mode's letterbox grows
                // and retracts over several ticks -- and the viewport and the
                // scissor that crops to it stepped at the game's rate while
                // the scene inside them glided at the display's. Blend both
                // between the matched frames, under the same gate as the
                // projection matrix, so a declared cut (pose or field of
                // view) snaps them with everything else.
                if (interpolateProjection && (prevWorkloadPtr != nullptr)) {
                    const auto &prevViewports = prevWorkloadPtr->drawData.rspViewports;
                    if ((prevTransformIndex < prevViewports.size()) &&
                        (proj.transformsIndex < drawData.modRspViewports.size())) {
                        const interop::RSPViewport &prevVp = prevViewports[prevTransformIndex];
                        const interop::RSPViewport &curVp = drawData.rspViewports[proj.transformsIndex];
                        interop::RSPViewport &modVp = drawData.modRspViewports[proj.transformsIndex];
                        const float w = p.curFrameWeight;
                        auto lerpF = [w](float prev, float cur) { return prev + (cur - prev) * w; };
                        modVp.scale.x = lerpF(prevVp.scale.x, curVp.scale.x);
                        modVp.scale.y = lerpF(prevVp.scale.y, curVp.scale.y);
                        modVp.scale.z = lerpF(prevVp.scale.z, curVp.scale.z);
                        modVp.translate.x = lerpF(prevVp.translate.x, curVp.translate.x);
                        modVp.translate.y = lerpF(prevVp.translate.y, curVp.translate.y);
                        modVp.translate.z = lerpF(prevVp.translate.z, curVp.translate.z);
                    }

                    // The previous frame's scissor for this camera: the
                    // projection in the previous workload that used the same
                    // matched transform.
                    for (uint32_t pf = 0; pf < prevWorkloadPtr->fbPairCount; pf++) {
                        const FramebufferPair &prevFbPair = prevWorkloadPtr->fbPairs[pf];
                        for (uint32_t pp = 0; pp < prevFbPair.projectionCount; pp++) {
                            const Projection &prevProj = prevFbPair.projections[pp];
                            if ((prevProj.type == proj.type) && (prevProj.transformsIndex == prevTransformIndex) &&
                                !prevProj.scissorRect.isNull()) {
                                const float w = p.curFrameWeight;
                                auto lerpCoord = [w](int32_t prev, int32_t cur) {
                                    return int32_t(std::lround(float(prev) + (float(cur) - float(prev)) * w));
                                };
                                proj.snapBlendedScissor.ulx = lerpCoord(prevProj.scissorRect.ulx, proj.scissorRect.ulx);
                                proj.snapBlendedScissor.uly = lerpCoord(prevProj.scissorRect.uly, proj.scissorRect.uly);
                                proj.snapBlendedScissor.lrx = lerpCoord(prevProj.scissorRect.lrx, proj.scissorRect.lrx);
                                proj.snapBlendedScissor.lry = lerpCoord(prevProj.scissorRect.lry, proj.scissorRect.lry);
                                proj.snapScissorBlended = true;
                                pf = prevWorkloadPtr->fbPairCount;
                                break;
                            }
                        }
                    }
                }
            }
            else {
                prevViewTransform = viewMatrix;
                prevProjTransform = projMatrix;
            }

            viewProjMatrix = hlslpp::mul(viewMatrix, projMatrix);

            interop::float4x4 &prevViewProjTransform = drawData.prevViewProjTransforms[proj.transformsIndex];
            prevViewProjTransform = hlslpp::mul(prevViewTransform, prevProjTransform);
        }
    }

    void ProjectionProcessor::upload(const ProcessParams &p) {
        uploads.clear();

        for (uint32_t w : p.curFrame->workloads) {
            Workload &workload = p.workloadQueue->workloads[w];
            const DrawData &drawData = workload.drawData;
            DrawBuffers &drawBuffers = workload.drawBuffers;
            std::pair<size_t, size_t> uploadRange = { 0, drawData.viewProjTransforms.size() };
            uploads.emplace_back(BufferUploader::Upload{ drawData.modViewProjTransforms.data(), uploadRange, sizeof(interop::float4x4), RenderBufferFlag::STORAGE, { }, &drawBuffers.viewProjTransformsBuffer });

            // The viewports as blended for this sub-frame, into the same
            // buffer the RSP vertex path reads; the workload's own one-time
            // upload wrote the raw values and this overwrites them each
            // sub-frame, blended or not.
            std::pair<size_t, size_t> viewportRange = { 0, drawData.modRspViewports.size() };
            if (viewportRange.second > 0) {
                uploads.emplace_back(BufferUploader::Upload{ drawData.modRspViewports.data(), viewportRange, sizeof(interop::RSPViewport), RenderBufferFlag::STORAGE, { }, &drawBuffers.rspViewportsBuffer });
            }
        }

        bufferUploader->submit(p.worker, uploads);
    }
};