// Included inside snap::vr after the shared mesh and hand skinning helpers.
using namespace plume;
struct Props::Impl {
    VRDevice* device; VRQueue* queue;
    std::unique_ptr<RenderCommandList> list;
    std::unique_ptr<RenderCommandFence> fence;
    std::unique_ptr<RenderPipelineLayout> root,presentationRoot;
    std::unique_ptr<RenderPipelineLayout> copyRoot;
    std::unique_ptr<RenderPipeline> copyPipeline;
    std::unique_ptr<RenderDescriptorSet> copyDescriptors;
    std::unique_ptr<RenderPipeline> pipeline,transparentPipeline,presentationPipeline;
    std::unique_ptr<RenderDescriptorSet> descriptors;
    std::unique_ptr<RenderSampler> sampler;
    std::unique_ptr<RenderBuffer> vertices,indices;
    std::unique_ptr<RenderTexture> fluteTexture,blankTexture;
    std::unique_ptr<RenderQueryPool> timestamps;
    double waitMs=0;
    size_t capacity=0,indexCapacity=0;
    std::array<Rig,2> hands;
    Mesh cameraMesh,vehicleMesh,appleMesh,pesterBallMesh;
    struct RigidDraw {const Mesh* mesh;Pose pose;bool mirror;float press;};
    std::vector<RigidDraw> rigidDraws;
    std::unordered_map<const Mesh*,std::unique_ptr<RenderBuffer>> rigidBuffers;
    uint64_t uploadedGeometry=UINT64_MAX;
    std::vector<Vertex> frameVertices;
    std::array<std::vector<Vertex>,2> contactVertices;
    std::array<bool,2> contactReady{};
    uint64_t contactFrame=0;
    std::vector<unsigned> opaqueIndices;
    std::vector<TransparentTriangle> transparentTriangles;
    uint64_t geometryFrame=0;bool geometryReady=false;
    double flutePressTime=-100;
    RenderInputSlot slot{0,sizeof(Vertex)};

    std::unique_ptr<RenderShader> loadShader(const char* name,const char* entry) {
        auto path=snap::exe_path(std::string("shaders/quest/")+name);
        std::ifstream file(path,std::ios::binary|std::ios::ate);
        if(!file)throw std::runtime_error("Missing Quest shader: "+path.string());
        const auto size=file.tellg();std::vector<uint32_t> code((size_t(size)+3)/4);
        file.seekg(0);file.read(reinterpret_cast<char*>(code.data()),size);
        if(!file)throw std::runtime_error("Cannot read Quest shader");
        return device->createShader(code.data(),size_t(size),entry,RenderShaderFormat::SPIRV);
    }
    Impl(VRDevice* d,VRQueue* q):device(d),queue(q) {
        list=q->createCommandList();fence=d->createCommandFence();timestamps=d->createQueryPool(2);
        RenderDescriptorRange ranges[3]{};
        for(unsigned i=0;i<3;i++){ranges[i].type=i==2?RenderDescriptorRangeType::SAMPLER:RenderDescriptorRangeType::TEXTURE;ranges[i].binding=i;ranges[i].count=1;}
        RenderDescriptorSetDesc ds{ranges,3};descriptors=d->createDescriptorSet(ds);
        RenderPushConstantRange push{0,0,0,80,RenderShaderStageFlag::VERTEX};
        root=d->createPipelineLayout({&push,1,&ds,1,false,true});
        RenderSamplerDesc sd{};sd.minFilter=sd.magFilter=RenderFilter::LINEAR;
        sd.addressU=sd.addressV=sd.addressW=RenderTextureAddressMode::CLAMP;
        sampler=d->createSampler(sd);descriptors->setSampler(2,sampler.get());
        auto vs=loadShader("props.vs.spv","vs"),ps=loadShader("props.ps.spv","ps");
        RenderInputElement elements[]={
            {"POSITION",0,0,RenderFormat::R32G32B32_FLOAT,0,0},
            {"COLOR",0,1,RenderFormat::R32G32B32_FLOAT,0,12},
            {"TEXCOORD",0,2,RenderFormat::R32G32B32_FLOAT,0,24},
            {"COLOR",1,3,RenderFormat::R32_FLOAT,0,36}};
        RenderGraphicsPipelineDesc pd{};pd.pipelineLayout=root.get();pd.vertexShader=vs.get();pd.pixelShader=ps.get();
        pd.inputSlots=&slot;pd.inputSlotsCount=1;pd.inputElements=elements;pd.inputElementsCount=4;
        pd.depthEnabled=pd.depthWriteEnabled=pd.depthClipEnabled=true;pd.depthFunction=RenderComparisonFunction::LESS_EQUAL;
        pd.renderTargetCount=1;pd.renderTargetFormat[0]=RenderFormat::R8G8B8A8_UNORM;
        pd.depthTargetFormat=RenderFormat::D32_FLOAT;pd.renderTargetBlend[0]=RenderBlendDesc::Copy();
        pipeline=d->createGraphicsPipeline(pd);
        pd.depthWriteEnabled=false;pd.renderTargetBlend[0]=RenderBlendDesc::AlphaBlend();
        transparentPipeline=d->createGraphicsPipeline(pd);
        push.size=96;push.stageFlags=RenderShaderStageFlag::PIXEL;
        presentationRoot=d->createPipelineLayout({&push,1,nullptr,0});
        vs=loadShader("presentation.vs.spv","vs");ps=loadShader("presentation.ps.spv","ps");
        pd.pipelineLayout=presentationRoot.get();pd.vertexShader=vs.get();pd.pixelShader=ps.get();
        pd.inputSlots=nullptr;pd.inputSlotsCount=0;pd.inputElements=nullptr;pd.inputElementsCount=0;
        pd.depthEnabled=false;pd.depthTargetFormat=RenderFormat::UNKNOWN;
        presentationPipeline=d->createGraphicsPipeline(pd);
        RenderDescriptorSetDesc copySet{ranges,1};
        copyRoot=d->createPipelineLayout({nullptr,0,&copySet,1});
        copyDescriptors=d->createDescriptorSet(copySet);
        vs=loadShader("copy.vs.spv","vs");ps=loadShader("copy.ps.spv","ps");
        pd.pipelineLayout=copyRoot.get();pd.vertexShader=vs.get();pd.pixelShader=ps.get();
        pd.renderTargetBlend[0]=RenderBlendDesc::Copy();pd.depthWriteEnabled=false;
        copyPipeline=d->createGraphicsPipeline(pd);
        FluteIcon blank{};uploadIcon(blank,blankTexture);
        std::ifstream file(snap::base_dir()/"assets/vr/hands.json");if(!file)throw std::runtime_error("Missing assets/vr/hands.json");
        nlohmann::json j;file>>j;hands[0].load(j.at("left"));hands[1].load(j.at("right"));
        std::ifstream models(snap::base_dir()/"assets/vr/props.json");if(!models)throw std::runtime_error("Missing assets/vr/props.json");
        nlohmann::json props;models>>props;cameraMesh.load(props.at("models").at("camera"));vehicleMesh.load(props.at("models").at("zero_one"));
        appleMesh.load(props.at("models").at("apple"));pesterBallMesh.load(props.at("models").at("pester_ball"));
    }
    void begin(){list->begin();}
    void finish(){
        list->end();static_cast<RenderCommandQueue*>(queue)->executeCommandLists(list.get(),fence.get());
        auto start=std::chrono::steady_clock::now();queue->waitForCommandFence(fence.get());
        waitMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    }
    void uploadIcon(const FluteIcon& icon,std::unique_ptr<RenderTexture>& texture) {
        constexpr unsigned side=FluteIcon::side;
        texture=device->createTexture(RenderTextureDesc::Texture2D(side,side,1,RenderFormat::R8G8B8A8_UNORM));
        auto upload=device->createBuffer(RenderBufferDesc::UploadBuffer(side*side*4));
        auto* pixels=static_cast<uint8_t*>(upload->map());
        for(unsigned i=0;i<side*side;i++){
            for(unsigned c=0;c<3;c++)pixels[i*4+c]=uint8_t((unsigned(icon.rgba[i*4+c])*icon.rgba[i*4+3]+127)/255);
            pixels[i*4+3]=icon.rgba[i*4+3];
        }
        upload->unmap();begin();
        list->barriers(RenderBarrierStage::COPY,RenderTextureBarrier(texture.get(),RenderTextureLayout::COPY_DEST));
        list->copyTextureRegion(RenderTextureCopyLocation::Subresource(texture.get()),RenderTextureCopyLocation::PlacedFootprint(upload.get(),RenderFormat::R8G8B8A8_UNORM,side,side,1,side));
        list->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(texture.get(),RenderTextureLayout::SHADER_READ));finish();
    }
    void uploadFluteIcon(const FluteIcon& icon){uploadIcon(icon,fluteTexture);}
    std::unique_ptr<RenderFramebuffer> framebuffer(VRTexture* color,VRTexture* depth=nullptr) {
        const RenderTexture* colors[]={color};
        return device->createFramebuffer({colors,1,depth});
    }
    void viewport(VRTexture* color) {
        list->setViewports(RenderViewport{0,0,float(color->desc.width),float(color->desc.height)});
        list->setScissors(RenderRect{0,0,int32_t(color->desc.width),int32_t(color->desc.height)});
    }
    void draw(VRTexture* color,VRTexture* depth,VRTexture* screen,const std::vector<Vertex>& v,const std::vector<unsigned>& transparent,Pose eye,Fov fov) {
        size_t size=v.size()*sizeof(Vertex),indexSize=(opaqueIndices.size()+transparent.size())*sizeof(unsigned);
        if(size>capacity){capacity=size*2;vertices=device->createBuffer(RenderBufferDesc::VertexBuffer(capacity,RenderHeapType::UPLOAD));}
        if(size&&uploadedGeometry!=geometryFrame){std::memcpy(vertices->map(),v.data(),size);vertices->unmap();uploadedGeometry=geometryFrame;}
        if(indexSize>indexCapacity){indexCapacity=indexSize*2;indices=device->createBuffer(RenderBufferDesc::IndexBuffer(indexCapacity,RenderHeapType::UPLOAD));}
        if(indexSize){auto* data=static_cast<unsigned*>(indices->map());std::copy(opaqueIndices.begin(),opaqueIndices.end(),data);std::copy(transparent.begin(),transparent.end(),data+opaqueIndices.size());indices->unmap();}
        for(const auto& draw:rigidDraws)if(!rigidBuffers.contains(draw.mesh)) {
            auto buffer=device->createBuffer(RenderBufferDesc::VertexBuffer(draw.mesh->vertices.size()*sizeof(Vertex),RenderHeapType::UPLOAD));
            auto* data=static_cast<Vertex*>(buffer->map());
            std::copy(draw.mesh->vertices.begin(),draw.mesh->vertices.end(),data);
            for(size_t i=0;i<draw.mesh->vertices.size();++i)if(draw.mesh->shutter[i])data[i].textured=-1;
            buffer->unmap();rigidBuffers[draw.mesh]=std::move(buffer);
        }
        descriptors->setTexture(0,screen,RenderTextureLayout::SHADER_READ);
        descriptors->setTexture(1,fluteTexture?fluteTexture.get():blankTexture.get(),RenderTextureLayout::SHADER_READ);
        auto fb=framebuffer(color,depth);begin();
        RenderTextureBarrier barriers[]={{screen,RenderTextureLayout::SHADER_READ},{color,RenderTextureLayout::COLOR_WRITE},{depth,RenderTextureLayout::DEPTH_WRITE}};
        list->barriers(RenderBarrierStage::GRAPHICS,barriers,3);
        list->setFramebuffer(fb.get());viewport(color);
        list->setPipeline(pipeline.get());list->setGraphicsPipelineLayout(root.get());list->setGraphicsDescriptorSet(descriptors.get(),0);
        auto vp=multiply(view(eye),projection(fov,.02f,1000));
        struct Constants {Matrix matrix;float deformation[4];};
        static_assert(sizeof(Constants)==80);
        for(const auto& draw:rigidDraws) {
            auto model=transform(draw.pose);
            if(draw.mirror)for(unsigned c=0;c<4;++c)model[c]=-model[c];
            Constants constants{multiply(model,vp),{draw.press,0,0,0}};
            list->setGraphicsPushConstants(0,&constants);
            RenderVertexBufferView vb{rigidBuffers.at(draw.mesh)->at(0),uint32_t(draw.mesh->vertices.size()*sizeof(Vertex))};
            list->setVertexBuffers(0,&vb,1,&slot);list->drawInstanced(uint32_t(draw.mesh->vertices.size()),1,0,0);
        }
        if(size&&indexSize) {
            Constants constants{vp,{0,0,0,0}};list->setGraphicsPushConstants(0,&constants);
            RenderVertexBufferView vb{vertices->at(0),uint32_t(size)};list->setVertexBuffers(0,&vb,1,&slot);
            RenderIndexBufferView ib{indices->at(0),uint32_t(indexSize),RenderFormat::R32_UINT};list->setIndexBuffer(&ib);
            list->drawIndexedInstanced(uint32_t(opaqueIndices.size()),1,0,0,0);
            if(!transparent.empty()){list->setPipeline(transparentPipeline.get());list->drawIndexedInstanced(uint32_t(transparent.size()),1,uint32_t(opaqueIndices.size()),0,0);}
        }
        finish();
    }
};
Props::Props(VRDevice* d,VRQueue* q):impl(std::make_unique<Impl>(d,q)){}
Props::~Props()=default;
