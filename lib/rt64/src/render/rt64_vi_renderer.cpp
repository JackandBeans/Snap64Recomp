//
// RT64
//

#include "rt64_vi_renderer.h"

#include <algorithm>

#include "shared/rt64_hlsl.h"
#include "shared/rt64_video_interface.h"

namespace RT64 {
    // VIRenderer

    VIRenderer::VIRenderer() { }

    VIRenderer::~VIRenderer() { }

    inline hlslpp::float2 computeHDSize(hlslpp::float2 sdSize, hlslpp::float2 resolutionScale, uint32_t downsamplingScale) {
        return (sdSize * resolutionScale) / float(downsamplingScale);
    }

    inline hlslpp::float2 fromSDtoHD(hlslpp::float2 coordinate, hlslpp::float2 sdSize, hlslpp::float2 hdSize) {
        const hlslpp::float2 relativeScale = hdSize / sdSize;
        return coordinate * relativeScale;
    }

    inline hlslpp::float2 fromHDtoWindow(hlslpp::float2 coordinate, hlslpp::float2 hdSize, hlslpp::float2 windowSize) {
        const hlslpp::float2 hdCenter = hdSize / 2;
        const hlslpp::float2 windowCenter = windowSize / 2;
        const hlslpp::float2 relativeCoordinate = { coordinate.x - hdCenter.x, coordinate.y - hdCenter.y };

        // Window is wider than virtual HD TV, we do pillarboxing.
        float relativeScale;
        if ((windowSize.x / windowSize.y) > (hdSize.x / hdSize.y)) {
            relativeScale = windowSize.y / hdSize.y;
        }
        // Window is taller than virtual HD TV, we do letterboxing.
        else {
            relativeScale = windowSize.x / hdSize.x;
        }

        return windowCenter + relativeCoordinate * relativeScale;
    }

    void VIRenderer::render(const RenderParams &p) {
        const ShaderRecord *shader = nullptr;
        const RenderSampler *sampler = nullptr;
        switch (p.filtering) {
        case UserConfiguration::Filtering::Nearest:
            shader = &p.shaderLibrary->videoInterfaceNearest;
            sampler = p.shaderLibrary->samplerLibrary.nearest.borderBorder.get();
            break;
        case UserConfiguration::Filtering::AntiAliasedPixelScaling:
            shader = &p.shaderLibrary->videoInterfacePixel;
            sampler = p.shaderLibrary->samplerLibrary.linear.borderBorder.get();
            break;
        case UserConfiguration::Filtering::Linear:
        default:
            shader = &p.shaderLibrary->videoInterfaceLinear;
            sampler = p.shaderLibrary->samplerLibrary.linear.borderBorder.get();
            break;
        }

        if ((descriptorSet == nullptr) || (descriptorSetSampler != sampler)) {
            descriptorSet = std::make_unique<VideoInterfaceDescriptorSet>(sampler, p.device);
            descriptorSetSampler = sampler;
        }

        descriptorSet->setTexture(descriptorSet->gInput, p.texture, RenderTextureLayout::SHADER_READ);

        RenderViewport viewport;
        RenderRect scissor;
        getViewportAndScissor(p.swapChain, *p.vi, p.resolutionScale, p.downsamplingScale, p.removeBlackBorders, p.crop, viewport, scissor);
        p.commandList->setViewports(viewport);
        p.commandList->setScissors(scissor);

        interop::VideoInterfaceCB pushConstants;
        pushConstants.videoResolution = computeHDSize(hlslpp::float2(p.vi->fbSize()), p.resolutionScale, p.downsamplingScale);
        pushConstants.textureResolution = { float(p.textureWidth), float(p.textureHeight) };
        pushConstants.gamma = p.vi->gamma();

        p.commandList->setPipeline(shader->pipeline.get());
        p.commandList->setGraphicsPipelineLayout(shader->pipelineLayout.get());
        p.commandList->setGraphicsDescriptorSet(descriptorSet->get(), 0);
        p.commandList->setGraphicsPushConstants(0, &pushConstants);
        p.commandList->setVertexBuffers(0, nullptr, 0, nullptr);
        p.commandList->drawInstanced(3, 1, 0, 0);
    }

    void VIRenderer::getViewportAndScissor(const RenderSwapChain *swapChain, const VI &vi, hlslpp::float2 resolutionScale, uint32_t downsamplingScale, bool removeBlackBorders, const uint32_t crop[4], RenderViewport &viewport, RenderRect &scissor) {
        // We define three different coordinate spaces to work with to translate the VI parameters into the Window.
        //
        // VideoSD: This corresponds to the SD TV Scanline space, which is what the VI natively works on.
        // 
        // VideoHD: This corresponds to what the imaginary "HD" TV would be if it supported the amount of scanlines
        // desired by the resolution scale (divided by the downsampling scale) and a wider aspect ratio.
        // 
        // Window: The native coordinate space of the device's render target.
        //
        // The provided buffer doesn't necessarily have the same dimensions that the VI will sample to display it 
        // on the screen. To work around that, the viewport the buffer will be drawn in will be expanded so only
        // the region of interest is rendered. A scissor will cut it off correctly according to the coordinates
        // specified by the VI.
        const hlslpp::float2 sdSize = removeBlackBorders ? hlslpp::float2(vi.fbSize()) : hlslpp::float2(320.0f, 240.0f);
        const hlslpp::float2 hdSize = computeHDSize(sdSize, resolutionScale, downsamplingScale);
        const hlslpp::float2 windowSize = { float(swapChain->getWidth()), float(swapChain->getHeight()) };

        // Query the VI for the current rendering area.
        hlslpp::float4 viViewRect = vi.viewRectangle() * sdSize.xyxy;
        hlslpp::float4 viCropRect = vi.cropRectangle() * sdSize.xyxy;

        // Scale all the rectangles to the space of the Window.
        hlslpp::float2 topLeftViewport = fromSDtoHD({ float(viViewRect.x), float(viViewRect.y) }, sdSize, hdSize);
        hlslpp::float2 bottomRightViewport = fromSDtoHD({ float(viViewRect.x + viViewRect.z), float(viViewRect.y + viViewRect.w) }, sdSize, hdSize);
        topLeftViewport = fromHDtoWindow(topLeftViewport, hdSize, windowSize);
        bottomRightViewport = fromHDtoWindow(bottomRightViewport, hdSize, windowSize);

        hlslpp::float2 topLeftScissor = fromSDtoHD({ float(viCropRect.x), float(viCropRect.y) }, sdSize, hdSize);
        hlslpp::float2 bottomRightScissor = fromSDtoHD({ float(viCropRect.x + viCropRect.z), float(viCropRect.y + viCropRect.w) }, sdSize, hdSize);
        topLeftScissor = fromHDtoWindow(topLeftScissor, hdSize, windowSize);
        bottomRightScissor = fromHDtoWindow(bottomRightScissor, hdSize, windowSize);

        viewport = RenderViewport(topLeftViewport.x, topLeftViewport.y, bottomRightViewport.x - topLeftViewport.x, bottomRightViewport.y - topLeftViewport.y);
        scissor = RenderRect(lround(topLeftScissor.x), lround(topLeftScissor.y), lround(bottomRightScissor.x), lround(bottomRightScissor.y));

        // Pokemon Snap port: overscan crop. The game leaves dead margins in
        // its framebuffer -- black in scenes, stale bytes in menus -- and its
        // one VI mode never compensates, because the CRTs it was authored for
        // cropped the picture's edges on every set sold. Hiding a margin of
        // the buffer the same way: the viewport grows so the cropped region
        // fills the same fit area, and the scissor still clips to that area,
        // so nothing outside the fit rectangle is touched.
        if (crop != nullptr) {
            float cropL = float(crop[0]);
            float cropR = float(crop[1]);
            const float cropT = float(crop[2]);
            const float cropB = float(crop[3]);

            // Under the widescreen expansion the horizontal edges of the
            // image are newly rendered extra field of view, not the game's
            // authored dead columns, so a horizontal crop would trim real
            // picture. The expansion widens only X of the resolution scale,
            // which is how it announces itself here. The vertical margins
            // are untouched by the expansion and still deserve the crop.
            if (float(resolutionScale.x) > float(resolutionScale.y) * 1.001f) {
                cropL = 0.0f;
                cropR = 0.0f;
            }

            const float keptW = float(sdSize.x) - cropL - cropR;
            const float keptH = float(sdSize.y) - cropT - cropB;

            // The quarter-size floor bounds the zoom at 4x, which keeps the
            // scaled viewport far inside the API's coordinate limits no
            // matter what a hand-edited config asks for.
            if (((cropL + cropR + cropT + cropB) > 0.0f) &&
                (keptW >= float(sdSize.x) * 0.25f) && (keptH >= float(sdSize.y) * 0.25f)) {
                // One uniform scale for both axes: the kept region keeps its
                // proportions and is centered, with the smaller axis
                // letterboxed inside the fit area, instead of unequal crops
                // stretching the picture.
                const float scale = std::min(float(sdSize.x) / keptW, float(sdSize.y) / keptH);
                const float newWidth = viewport.width * scale;
                const float newHeight = viewport.height * scale;
                const float keptScreenW = (keptW / float(sdSize.x)) * newWidth;
                const float keptScreenH = (keptH / float(sdSize.y)) * newHeight;
                viewport.x += (viewport.width - keptScreenW) * 0.5f - (cropL / float(sdSize.x)) * newWidth;
                viewport.y += (viewport.height - keptScreenH) * 0.5f - (cropT / float(sdSize.y)) * newHeight;
                viewport.width = newWidth;
                viewport.height = newHeight;

                // Whatever the fit area shows outside the kept region is
                // exactly the dead margin the crop exists to hide, and only
                // the scissor blacks it out. Without this an unequal crop
                // was a visible no-op: the uniform scale comes out at 1.0,
                // the viewport merely re-centers, and the margins stay on
                // screen inside the untouched scissor.
                const int32_t keptLeft = int32_t(lround(viewport.x + (cropL / float(sdSize.x)) * newWidth));
                const int32_t keptTop = int32_t(lround(viewport.y + (cropT / float(sdSize.y)) * newHeight));
                const int32_t keptRight = int32_t(lround(viewport.x + ((cropL + keptW) / float(sdSize.x)) * newWidth));
                const int32_t keptBottom = int32_t(lround(viewport.y + ((cropT + keptH) / float(sdSize.y)) * newHeight));
                scissor.left = std::max(scissor.left, keptLeft);
                scissor.top = std::max(scissor.top, keptTop);
                scissor.right = std::min(scissor.right, keptRight);
                scissor.bottom = std::min(scissor.bottom, keptBottom);
            }
        }
    }
};