//
// RT64
//

#include "rt64_rsp_processor.h"

#include "rt64_buffer_uploader.h"

namespace RT64 {
    // RSPProcessor

    RSPProcessor::RSPProcessor(RenderDevice *device) {
        processSet = std::make_unique<RSPProcessDescriptorSet>(device);
        modifySet = std::make_unique<RSPModifyDescriptorSet>(device);
        snapVrProcessSet[0] = std::make_unique<RSPProcessDescriptorSet>(device);
        snapVrProcessSet[1] = std::make_unique<RSPProcessDescriptorSet>(device);
    }

    RSPProcessor::~RSPProcessor() { }
    
    void RSPProcessor::process(const ProcessParams &p) {
        const DrawData &drawData = *p.drawData;
        const uint32_t drawVertexCount = drawData.vertexCount();
        processCB.vertexStart = uint32_t(p.outputBuffers->screenPosBuffer.computedSize / (sizeof(float) * 4));
        processCB.vertexCount = drawVertexCount - processCB.vertexStart;
        processCB.prevFrameWeight = p.prevFrameWeight;
        processCB.curFrameWeight = p.curFrameWeight;
        p.outputBuffers->screenPosBuffer.computedSize += processCB.vertexCount * sizeof(float) * 4;
        p.outputBuffers->genTexCoordBuffer.computedSize += processCB.vertexCount * sizeof(float) * 2;
        p.outputBuffers->shadedColBuffer.computedSize += processCB.vertexCount * sizeof(float) * 4;
        processSet->setBuffer(processSet->srcPos, p.drawBuffers->positionBuffer.get(), p.drawBuffers->positionBuffer.getView(0));
        processSet->setBuffer(processSet->srcVel, p.drawBuffers->velocityBuffer.get(), p.drawBuffers->velocityBuffer.getView(0));
        processSet->setBuffer(processSet->srcTc, p.drawBuffers->texcoordBuffer.get(), p.drawBuffers->texcoordBuffer.getView(0));
        processSet->setBuffer(processSet->srcTcVel, p.drawBuffers->texcoordVelocityBuffer.get(), p.drawBuffers->texcoordVelocityBuffer.getView(0));
        processSet->setBuffer(processSet->srcCol, p.drawBuffers->normalColorBuffer.get(), p.drawBuffers->normalColorBuffer.getView(0));
        processSet->setBuffer(processSet->srcNorm, p.drawBuffers->normalColorBuffer.get(), p.drawBuffers->normalColorBuffer.getView(1));
        processSet->setBuffer(processSet->srcViewProjIndices, p.drawBuffers->viewProjIndicesBuffer.get(), p.drawBuffers->viewProjIndicesBuffer.getView(0));
        processSet->setBuffer(processSet->srcWorldIndices, p.drawBuffers->worldIndicesBuffer.get(), p.drawBuffers->worldIndicesBuffer.getView(0));
        processSet->setBuffer(processSet->srcFogIndices, p.drawBuffers->fogIndicesBuffer.get(), p.drawBuffers->fogIndicesBuffer.getView(0));
        processSet->setBuffer(processSet->srcLightIndices, p.drawBuffers->lightIndicesBuffer.get(), p.drawBuffers->lightIndicesBuffer.getView(0));
        processSet->setBuffer(processSet->srcLightCounts, p.drawBuffers->lightCountsBuffer.get(), p.drawBuffers->lightCountsBuffer.getView(0));
        processSet->setBuffer(processSet->srcLookAtIndices, p.drawBuffers->lookAtIndicesBuffer.get(), p.drawBuffers->lookAtIndicesBuffer.getView(0));
        processSet->setBuffer(processSet->rspViewportVector, p.drawBuffers->rspViewportsBuffer.get(), RenderBufferStructuredView(sizeof(interop::RSPViewport)));
        processSet->setBuffer(processSet->rspFogVector, p.drawBuffers->rspFogBuffer.get(), RenderBufferStructuredView(sizeof(interop::RSPFog)));
        processSet->setBuffer(processSet->rspLightVector, p.drawBuffers->rspLightsBuffer.get(), RenderBufferStructuredView(sizeof(interop::RSPLight)));
        processSet->setBuffer(processSet->rspLookAtVector, p.drawBuffers->rspLookAtBuffer.get(), RenderBufferStructuredView(sizeof(interop::RSPLookAt)));
        processSet->setBuffer(processSet->viewProjTransforms, p.drawBuffers->viewProjTransformsBuffer.get(), RenderBufferStructuredView(sizeof(interop::float4x4)));
        processSet->setBuffer(processSet->worldTransforms, p.drawBuffers->worldTransformsBuffer.get(), RenderBufferStructuredView(sizeof(interop::float4x4)));
        processSet->setBuffer(processSet->dstPos, p.outputBuffers->screenPosBuffer.buffer.get(), RenderBufferStructuredView(sizeof(float) * 4));
        processSet->setBuffer(processSet->dstTc, p.outputBuffers->genTexCoordBuffer.buffer.get(), RenderBufferStructuredView(sizeof(float) * 2));
        processSet->setBuffer(processSet->dstCol, p.outputBuffers->shadedColBuffer.buffer.get(), RenderBufferStructuredView(sizeof(float) * 4));

        // Pokemon Snap port, the headset: an eye's set differs from the
        // picture's in the two buffers the projection processor filled for it.
        for (uint32_t e = 0; e < 2; e++) {
            RSPProcessDescriptorSet *eyeSet = snapVrProcessSet[e].get();
            const BufferPair &eyeViewProj = p.drawBuffers->snapVrViewProjTransformsBuffer[e];
            const BufferPair &eyeViewports = p.drawBuffers->snapVrRspViewportsBuffer[e];
            if ((eyeSet == nullptr) || (eyeViewProj.get() == nullptr) || (eyeViewports.get() == nullptr)) {
                continue;
            }

            eyeSet->setBuffer(eyeSet->srcPos, p.drawBuffers->positionBuffer.get(), p.drawBuffers->positionBuffer.getView(0));
            eyeSet->setBuffer(eyeSet->srcVel, p.drawBuffers->velocityBuffer.get(), p.drawBuffers->velocityBuffer.getView(0));
            eyeSet->setBuffer(eyeSet->srcTc, p.drawBuffers->texcoordBuffer.get(), p.drawBuffers->texcoordBuffer.getView(0));
            eyeSet->setBuffer(eyeSet->srcTcVel, p.drawBuffers->texcoordVelocityBuffer.get(), p.drawBuffers->texcoordVelocityBuffer.getView(0));
            eyeSet->setBuffer(eyeSet->srcCol, p.drawBuffers->normalColorBuffer.get(), p.drawBuffers->normalColorBuffer.getView(0));
            eyeSet->setBuffer(eyeSet->srcNorm, p.drawBuffers->normalColorBuffer.get(), p.drawBuffers->normalColorBuffer.getView(1));
            eyeSet->setBuffer(eyeSet->srcViewProjIndices, p.drawBuffers->viewProjIndicesBuffer.get(), p.drawBuffers->viewProjIndicesBuffer.getView(0));
            eyeSet->setBuffer(eyeSet->srcWorldIndices, p.drawBuffers->worldIndicesBuffer.get(), p.drawBuffers->worldIndicesBuffer.getView(0));
            eyeSet->setBuffer(eyeSet->srcFogIndices, p.drawBuffers->fogIndicesBuffer.get(), p.drawBuffers->fogIndicesBuffer.getView(0));
            eyeSet->setBuffer(eyeSet->srcLightIndices, p.drawBuffers->lightIndicesBuffer.get(), p.drawBuffers->lightIndicesBuffer.getView(0));
            eyeSet->setBuffer(eyeSet->srcLightCounts, p.drawBuffers->lightCountsBuffer.get(), p.drawBuffers->lightCountsBuffer.getView(0));
            eyeSet->setBuffer(eyeSet->srcLookAtIndices, p.drawBuffers->lookAtIndicesBuffer.get(), p.drawBuffers->lookAtIndicesBuffer.getView(0));
            eyeSet->setBuffer(eyeSet->rspViewportVector, eyeViewports.get(), RenderBufferStructuredView(sizeof(interop::RSPViewport)));
            eyeSet->setBuffer(eyeSet->rspFogVector, p.drawBuffers->rspFogBuffer.get(), RenderBufferStructuredView(sizeof(interop::RSPFog)));
            eyeSet->setBuffer(eyeSet->rspLightVector, p.drawBuffers->rspLightsBuffer.get(), RenderBufferStructuredView(sizeof(interop::RSPLight)));
            eyeSet->setBuffer(eyeSet->rspLookAtVector, p.drawBuffers->rspLookAtBuffer.get(), RenderBufferStructuredView(sizeof(interop::RSPLookAt)));
            eyeSet->setBuffer(eyeSet->viewProjTransforms, eyeViewProj.get(), RenderBufferStructuredView(sizeof(interop::float4x4)));
            eyeSet->setBuffer(eyeSet->worldTransforms, p.drawBuffers->worldTransformsBuffer.get(), RenderBufferStructuredView(sizeof(interop::float4x4)));
            eyeSet->setBuffer(eyeSet->dstPos, p.outputBuffers->screenPosBuffer.buffer.get(), RenderBufferStructuredView(sizeof(float) * 4));
            eyeSet->setBuffer(eyeSet->dstTc, p.outputBuffers->genTexCoordBuffer.buffer.get(), RenderBufferStructuredView(sizeof(float) * 2));
            eyeSet->setBuffer(eyeSet->dstCol, p.outputBuffers->shadedColBuffer.buffer.get(), RenderBufferStructuredView(sizeof(float) * 4));
        }

        const uint32_t modifyCount = drawData.modifyCount();
        modifyCB.modifyCount = modifyCount;
        modifySet->setBuffer(modifySet->srcModifyPos, p.drawBuffers->modifyPosUintsBuffer.get(), p.drawBuffers->modifyPosUintsBuffer.getView(0));
        modifySet->setBuffer(modifySet->screenPos, p.outputBuffers->screenPosBuffer.buffer.get(), RenderBufferStructuredView(sizeof(float) * 4));
    }

    void RSPProcessor::recordCommandList(RenderWorker *worker, const ShaderLibrary *shaderLibrary, const OutputBuffers *outputBuffers, int32_t snapVrEye) {
        // Pokemon Snap port, the headset: an eye's pass binds its own set.
        RSPProcessDescriptorSet *set = processSet.get();
        if ((snapVrEye >= 0) && (snapVrEye < 2) && (snapVrProcessSet[snapVrEye] != nullptr)) {
            set = snapVrProcessSet[snapVrEye].get();
        }

        const uint32_t ThreadGroupSize = 64;
        
        if (processCB.vertexCount > 0) {
            RenderBufferBarrier beforeBarriers[] = {
                RenderBufferBarrier(outputBuffers->screenPosBuffer.buffer.get(), RenderBufferAccess::WRITE),
                RenderBufferBarrier(outputBuffers->genTexCoordBuffer.buffer.get(), RenderBufferAccess::WRITE),
                RenderBufferBarrier(outputBuffers->shadedColBuffer.buffer.get(), RenderBufferAccess::WRITE)
            };

            RenderBufferBarrier afterBarriers[] = {
                RenderBufferBarrier(outputBuffers->screenPosBuffer.buffer.get(), RenderBufferAccess::READ),
                RenderBufferBarrier(outputBuffers->genTexCoordBuffer.buffer.get(), RenderBufferAccess::READ),
                RenderBufferBarrier(outputBuffers->shadedColBuffer.buffer.get(), RenderBufferAccess::READ)
            };

            const int dispatchCount = (processCB.vertexCount + ThreadGroupSize - 1) / ThreadGroupSize;
            worker->commandList->barriers(RenderBarrierStage::COMPUTE, beforeBarriers, uint32_t(std::size(beforeBarriers)));
            worker->commandList->setPipeline(shaderLibrary->rspProcess.pipeline.get());
            worker->commandList->setComputePipelineLayout(shaderLibrary->rspProcess.pipelineLayout.get());
            worker->commandList->setComputePushConstants(0, &processCB);
            worker->commandList->setComputeDescriptorSet(set->get(), 0);
            worker->commandList->dispatch(dispatchCount, 1, 1);
            worker->commandList->barriers(RenderBarrierStage::GRAPHICS, afterBarriers, uint32_t(std::size(afterBarriers)));
        }

        if (modifyCB.modifyCount > 0) {
            RenderBufferBarrier beforeBarrier = RenderBufferBarrier(outputBuffers->screenPosBuffer.buffer.get(), RenderBufferAccess::WRITE);
            RenderBufferBarrier afterBarrier = RenderBufferBarrier(outputBuffers->screenPosBuffer.buffer.get(), RenderBufferAccess::READ);
            const int dispatchCount = (modifyCB.modifyCount + ThreadGroupSize - 1) / ThreadGroupSize;
            worker->commandList->barriers(RenderBarrierStage::COMPUTE, beforeBarrier);
            worker->commandList->setPipeline(shaderLibrary->rspModify.pipeline.get());
            worker->commandList->setComputePipelineLayout(shaderLibrary->rspModify.pipelineLayout.get());
            worker->commandList->setComputePushConstants(0, &modifyCB);
            worker->commandList->setComputeDescriptorSet(modifySet->get(), 0);
            worker->commandList->dispatch(dispatchCount, 1, 1);
            worker->commandList->barriers(RenderBarrierStage::GRAPHICS, afterBarrier);
        }
    }
};
