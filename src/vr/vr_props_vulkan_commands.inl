void Props::copyImage(VRTexture* source,VRTexture* destination) {
    auto& x=*impl;x.begin();
    RenderTextureBarrier before[]={{source,RenderTextureLayout::COPY_SOURCE},{destination,RenderTextureLayout::COPY_DEST}};
    x.list->barriers(RenderBarrierStage::COPY,before,2);
    x.list->copyTextureRegion(RenderTextureCopyLocation::Subresource(destination),RenderTextureCopyLocation::Subresource(source));
    RenderTextureBarrier after[]={{source,RenderTextureLayout::COLOR_WRITE},{destination,RenderTextureLayout::COLOR_WRITE}};
    x.list->barriers(RenderBarrierStage::GRAPHICS,after,2);x.finish();
}
void Props::importSplash(VRTexture* color,VRTexture* depth,Pose eye,Fov fov,Pose panel,
        uint64_t frame,int state,int progress,const std::string& message,bool hover,Vec3 pointer,bool hasPointer) {
    auto& x=*impl;auto& v=x.frameVertices;
    if(!x.geometryReady||x.geometryFrame!=frame) {
        x.geometryReady=true;x.geometryFrame=frame;v.clear();x.rigidDraws.clear();x.opaqueIndices.clear();
        box(v,panel,{0,0,-.025f},{1.08f,.67f,.02f},{.035f,.065f,.11f});
        label(v,panel,"WELCOME TO SNAP64 VR",-.77f,.51f,.013f,{.95f,.8f,.25f});
        label(v,panel,"IMPORT YOUR POKEMON SNAP .Z64 ROM",-.91f,.30f,.009f,{1,1,1});
        label(v,panel,"USA REV 1 - UNMODIFIED ROM REQUIRED",-.91f,.18f,.0085f,{.8f,.88f,1});
        label(v,panel,"AFTER IMPORTING, THE GAME STARTS",-.91f,.05f,.0085f,{.8f,.88f,1});
        label(v,panel,"AUTOMATICALLY. SETUP IS ONLY NEEDED ONCE.",-.91f,-.05f,.0075f,{.8f,.88f,1});
        std::string text=message;
        for(char& c:text)if(static_cast<unsigned char>(c)>127)c=' ';
        for(unsigned line=0;!text.empty()&&line<3;line++) {
            size_t end=std::min<size_t>(text.size(),47);
            if(end<text.size()){auto space=text.rfind(' ',end);if(space!=std::string::npos&&space>15)end=space;}
            label(v,panel,text.substr(0,end).c_str(),-.91f,-.18f-line*.07f,.0065f,state==5?Color{1,.55f,.4f}:Color{.8f,.88f,1});
            text.erase(0,end);if(!text.empty()&&text.front()==' ')text.erase(0,1);
        }
        if(state==1||state==5) {
            box(v,panel,{0,-.46f,.012f},{.22f,.075f,.008f},hover?Color{.25f,.65f,.85f}:Color{.1f,.3f,.5f});
            Pose button=panel;button.position=panel.position+rotate(panel.orientation,{0,0,.025f});
            label(v,button,"OK",-.053f,-.427f,.009f,{1,1,1});
            label(v,panel,"POINT + TRIGGER, OR LOOK AT OK TO SELECT",-.91f,-.59f,.0075f,{.65f,.75f,.85f});
        } else if(state==3) {
            box(v,panel,{0,-.46f,.012f},{.9f,.04f,.005f},{.1f,.2f,.3f});
            float fraction=std::clamp(progress/100.f,0.f,1.f);
            if(fraction>0)box(v,panel,{-.9f+.9f*fraction,-.46f,.025f},{.9f*fraction,.033f,.005f},{.25f,.75f,.6f});
            char percent[16];std::snprintf(percent,sizeof(percent),"%d%%",progress);
            label(v,panel,percent,-.12f,-.56f,.009f,{1,1,1});
        }
        if(hasPointer&&(state==1||state==5))box(v,panel,{pointer.x,pointer.y,.06f},{.008f,.008f,.005f},{1,1,1});
        for(unsigned i=0;i<v.size();i++)x.opaqueIndices.push_back(i);
    }
    auto fb=x.framebuffer(color,depth);x.begin();
    RenderTextureBarrier barriers[]={{color,RenderTextureLayout::COLOR_WRITE},{depth,RenderTextureLayout::DEPTH_WRITE}};
    x.list->barriers(RenderBarrierStage::GRAPHICS,barriers,2);x.list->setFramebuffer(fb.get());
    x.list->clearColor(0,RenderColor(.012f,.018f,.03f,1));x.list->clearDepth();
    x.retainedFramebuffers.push_back(std::move(fb));x.finish();
    x.draw(color,depth,static_cast<VRTexture*>(x.blankTexture.get()),v,{},eye,fov);
}
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
        const bool fxaa=snap::settings().vr.fxaa;
        x.list->setPipeline(fxaa?x.fxaaPipeline.get():x.copyPipeline.get());x.list->setGraphicsPipelineLayout(x.copyRoot.get());
        const float constants[]={1.f/float(width),1.f/float(height),0,0};
        x.list->setGraphicsPushConstants(0,constants);
        x.list->setGraphicsDescriptorSet(descriptors.get(),0);x.list->drawInstanced(3,1,0,0);
        x.retainedFramebuffers.push_back(std::move(fb));x.finish();return;
    }
    if(snap::settings().vr.fxaa)throw std::runtime_error("FXAA requires a renderable OpenXR swapchain image view");
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
