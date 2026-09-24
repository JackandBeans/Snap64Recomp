void Props::presentation(VRTexture* color,Pose eye,Fov fov,float gain,bool portal,float height) {
    auto& x=*impl;
    Vec3 r=rotate(eye.orientation,{1,0,0}),u=rotate(eye.orientation,{0,1,0}),f=rotate(eye.orientation,{0,0,-1});
    float constants[]={eye.position.x,eye.position.y,eye.position.z,0,r.x,r.y,r.z,0,u.x,u.y,u.z,0,f.x,f.y,f.z,0,
        std::tan(fov.left),std::tan(fov.right),std::tan(fov.up),std::tan(fov.down),gain,portal?1.f:0.f,height,0};
    auto fb=x.framebuffer(color);x.begin();
    x.list->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(color,RenderTextureLayout::COLOR_WRITE));
    x.list->setFramebuffer(fb.get());x.viewport(color);
    x.list->setPipeline(x.presentationPipeline.get());x.list->setGraphicsPipelineLayout(x.presentationRoot.get());
    x.list->setGraphicsPushConstants(0,constants);x.list->drawInstanced(3,1,0,0);x.retainedFramebuffers.push_back(std::move(fb));x.finish();
}
void Props::beginTiming(){auto& x=*impl;x.drain();x.submission=x.retired=x.drawIndex=x.copyIndex=0;x.retainedFramebuffers.clear();x.retainedUploads.clear();x.deferred=true;x.waitMs=0;x.begin();x.list->resetQueryPool(x.timestamps.get(),0,2);x.list->writeTimestamp(x.timestamps.get(),0);x.finish();}
double Props::endTiming(bool deferCompletion){auto& x=*impl;x.begin();x.list->writeTimestamp(x.timestamps.get(),1);x.finish();return deferCompletion?-1:retireTiming();}
double Props::retireTiming(){auto& x=*impl;x.drain();x.deferred=false;x.submission=x.retired=0;x.timestamps->queryResults();auto* t=x.timestamps->getResults();return double(t[1]-t[0])/1e6;}
double Props::fenceWaitMs()const{return impl->waitMs;}
void Props::copy(VRTexture* source,VRTexture* destination,unsigned width,unsigned height) {
    auto& x=*impl;
    if(destination->imageView) {
        auto fb=x.framebuffer(destination);
        auto& descriptors=x.copyDescriptors.at(x.copyIndex++);
        descriptors->setTexture(0,source,RenderTextureLayout::SHADER_READ);
        x.begin();
        RenderTextureBarrier before[]={{source,RenderTextureLayout::SHADER_READ},{destination,RenderTextureLayout::COLOR_WRITE}};
        x.list->barriers(RenderBarrierStage::GRAPHICS,before,2);
        x.list->setFramebuffer(fb.get());x.viewport(destination);
        x.list->setPipeline(x.copyPipeline.get());x.list->setGraphicsPipelineLayout(x.copyRoot.get());
        x.list->setGraphicsDescriptorSet(descriptors.get(),0);x.list->drawInstanced(3,1,0,0);
        x.retainedFramebuffers.push_back(std::move(fb));x.finish();return;
    }
    x.begin();
    RenderTextureBarrier before[]={{source,RenderTextureLayout::COPY_SOURCE},{destination,RenderTextureLayout::COPY_DEST}};
    x.list->barriers(RenderBarrierStage::COPY,before,2);
    RenderBox box{0,0,int32_t(width),int32_t(height),0,1};
    x.list->copyTextureRegion(RenderTextureCopyLocation::Subresource(destination),RenderTextureCopyLocation::Subresource(source),0,0,0,&box);
    RenderTextureBarrier after[]={{source,RenderTextureLayout::COLOR_WRITE},{destination,RenderTextureLayout::COLOR_WRITE}};
    x.list->barriers(RenderBarrierStage::GRAPHICS,after,2);x.finish();
}
void Props::capture(VRTexture* source,const char* filename) {
    auto& x=*impl;const auto width=source->desc.width,height=source->desc.height;
    auto buffer=x.device->createBuffer(RenderBufferDesc::ReadbackBuffer(size_t(width)*height*4));
    auto old=source->textureLayout;x.begin();
    x.list->barriers(RenderBarrierStage::COPY,RenderTextureBarrier(source,RenderTextureLayout::COPY_SOURCE));
    x.list->copyTextureRegion(RenderTextureCopyLocation::PlacedFootprint(buffer.get(),source->desc.format,width,height,1,width),RenderTextureCopyLocation::Subresource(source));
    x.list->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(source,old));x.finish();x.drain();
    auto* data=buffer->map();int result=stbi_write_png(filename,int(width),int(height),4,data,int(width*4));buffer->unmap();
    if(!result)throw std::runtime_error("VR capture write failed");
}
