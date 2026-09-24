#include "vr_service.h"
#include "vr_openxr.h"
#include "vr_props.h"
#include "vr_options.h"
#include "vr_transition.h"
#include "settings.h"
#include "hle/rt64_workload_queue.h"
#include "render/rt64_render_target_manager.h"
#include "plume_d3d12.h"
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>

namespace snap {extern std::atomic<uint32_t> g_scene_overlay_rom;}
namespace snap::vr {
namespace {
using namespace RT64;
interop::float4x4 matrix(const Matrix& m){interop::float4x4 out;std::memcpy(&out,m.data(),sizeof(out));return out;}
struct Renderer {
    WorkloadQueue& queue;
    OpenXR xr;
    Interaction interaction;
    Options options;
    std::unique_ptr<Props> props;
    std::unique_ptr<FramebufferRenderer> renderer;
    std::unique_ptr<RSPProcessor> rsp;
    std::unique_ptr<BufferUploader> upload;
    std::array<std::unique_ptr<RenderTarget>,3> colors,depths;
    std::unique_ptr<RenderTarget> menuImage;
    std::array<RenderFramebufferStorage,3> framebuffers;
    bool ready=false;
    std::array<Held,2> previousHeld{};
    uint64_t previewFrame=0,nameTestFrame=0,modelTestFrame=0,fluteTestFrame=0;
    bool capturedHeadset=false;
    bool recenterDown=false, wasFocused=false, menuConfirmDown=false;
    Vec3 previousHeadForward{},previousLensForward{};
    bool hadTracking=false;
    float worldWeight=1;
    ViewTransition transition;
    unsigned diagnosticFrames=0,viewfinderTestFrames=0;
    bool stereoTest()const{return preview&&std::getenv("SNAP_VR_STEREO_TEST");}
    unsigned width(unsigned i)const{return preview?(stereoTest()?960:1280):xr.width(i);}
    unsigned height(unsigned i)const{return preview?(stereoTest()?1020:960):xr.height(i);}
    double accumulatedMs=0,accumulatedGpuMs=0,accumulatedWaitMs=0,workerWaitMs=0;uint64_t timedFrames=0;
    std::chrono::steady_clock::time_point submissionWindow{};
    unsigned submissionIntervals=0;
    void waitWorker(){auto start=std::chrono::steady_clock::now();queue.ext.workloadGraphicsWorker->wait();workerWaitMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
    Renderer(WorkloadQueue& q):queue(q) {
        if(q.ext.createdGraphicsAPI!=UserConfiguration::GraphicsAPI::D3D12)throw std::runtime_error("VR requires D3D12");
        auto* device=static_cast<plume::D3D12Device*>(q.ext.device);
        auto* nativeQueue=static_cast<plume::D3D12CommandQueue*>(q.ext.workloadGraphicsWorker->commandQueue.get());
        interaction.settings=snap::settings().vr;std::string error;
        if(!preview&&!xr.initialize(device->d3d,nativeQueue->d3d,interaction.settings.renderScale,error))throw std::runtime_error(error);
        props=std::make_unique<Props>(device->d3d,nativeQueue->d3d);
        renderer=std::make_unique<FramebufferRenderer>(q.ext.workloadGraphicsWorker,false,q.ext.createdGraphicsAPI,q.ext.shaderLibrary);
        rsp=std::make_unique<RSPProcessor>(q.ext.device);upload=std::make_unique<BufferUploader>(q.ext.device);
        for(unsigned i=0;i<3;i++) {
            colors[i]=std::make_unique<RenderTarget>(0,Framebuffer::Type::Color,RenderMultisampling{},false);
            depths[i]=std::make_unique<RenderTarget>(0,Framebuffer::Type::Depth,RenderMultisampling{},false);
            unsigned w=i==2?640:width(i),h=i==2?480:height(i);
            colors[i]->resize(q.ext.workloadGraphicsWorker,w,h);depths[i]->resize(q.ext.workloadGraphicsWorker,w,h);
            framebuffers[i].setup(q.ext.device,{},colors[i].get(),depths[i].get());
        }
        ready=true;printf("[SNAP-VR] %s initialized: %ux%u / %ux%u\n",preview?"diagnostic preview":"OpenXR",width(0),height(0),width(1),height(1));
    }
    void replay(GameFrame& frame,unsigned target,Pose eye,Fov fov,bool world,bool cinematic=false) {
        auto* worker=queue.ext.workloadGraphicsWorker;
        auto& color=*colors[target];auto& depth=*depths[target];
        worker->commandList->begin();
        color.clearColorTarget(worker);
        // The opening movie only authored scenery for its original camera. Supply
        // an unbounded fog-colored background behind it for both tracked eyes;
        // unlike a finite sky card this cannot expose black at its edges.
        // Keep depth untouched so all original scenery occludes the backdrop.
        if(cinematic && snap::g_scene_overlay_rom.load()==0xA08E30)
            worker->commandList->clearColor(0,plume::RenderColor(120.f/255.f,120.f/255.f,150.f/255.f,1.f));
        depth.clearDepthTarget(worker);
        worker->commandList->end();worker->execute();waitWorker();
        if(!world)return;
        for(uint32_t wi:frame.workloads) {
            auto& w=queue.workloads[wi];auto& d=w.drawData;
            std::vector<uint32_t> pairs;
            for(uint32_t f=0;f<w.fbPairCount;f++) {
                const auto& pair=w.fbPairs[f];if(pair.colorImage.width!=320)continue;
                bool perspective=false;for(uint32_t p=0;p<pair.projectionCount;p++)perspective|=pair.projections[p].type==Projection::Type::Perspective;
                if(perspective)pairs.push_back(f);
            }
            if(pairs.empty())continue;
            if(target==0&&std::getenv("SNAP_VR_STEREO_DIAG")) {
                static unsigned samples=0;
                if(samples++%120==0) {
                    std::vector<bool> replaced(d.viewProjTransforms.size(),false);
                    for(auto f:pairs)for(unsigned p=0;p<w.fbPairs[f].projectionCount;p++) {
                        const auto& projection=w.fbPairs[f].projections[p];
                        if(projection.type==Projection::Type::Perspective)replaced[projection.transformsIndex]=true;
                    }
                    unsigned missed=0,total=0;
                    for(auto f:pairs)for(unsigned p=0;p<w.fbPairs[f].projectionCount;p++) {
                        const auto& projection=w.fbPairs[f].projections[p];if(projection.type!=Projection::Type::Perspective)continue;
                        for(unsigned c=0;c<projection.gameCallCount;c++) {
                            const auto& call=projection.gameCalls[c];
                            for(unsigned n=0;n<call.callDesc.triangleCount*3;n++) {
                                auto vertex=d.faceIndices[call.meshDesc.faceIndicesStart+n];
                                auto index=d.viewProjIndices[vertex];total++;if(!replaced[index])missed++;
                            }
                        }
                    }
                    fprintf(stderr,"[SNAP-VR-STEREO] pairs %zu projections %zu vertices %u mono-source %u\n",pairs.size(),replaced.size(),total,missed);
                    for(size_t i=0;i<replaced.size();i++)if(replaced[i]) {
                        auto vp=d.rspViewports[i];fprintf(stderr,"[SNAP-VR-STEREO] vp %zu scale %.6f %.6f %.9f translate %.6f %.6f %.9f\n",i,float(vp.scale.x),float(vp.scale.y),float(vp.scale.z),float(vp.translate.x),float(vp.translate.y),float(vp.translate.z));
                    }
                }
            }
            // Only replace the render copies. Never alter the guest's camera matrices
            // or submit framebuffer copy/readback operations during an eye replay.
            auto savedView=d.modViewTransforms,savedProj=d.modProjTransforms,savedVP=d.modViewProjTransforms;
            auto savedViewport=d.modRspViewports;
            struct Restore {
                DrawData& d;
                decltype(savedView)& view;decltype(savedProj)& proj;decltype(savedVP)& vp;decltype(savedViewport)& viewport;
                ~Restore(){d.modViewTransforms=std::move(view);d.modProjTransforms=std::move(proj);d.modViewProjTransforms=std::move(vp);d.modRspViewports=std::move(viewport);}
            }restore{d,savedView,savedProj,savedVP,savedViewport};
            d.modViewTransforms=d.viewTransforms;d.modProjTransforms=d.projTransforms;d.modViewProjTransforms=d.viewProjTransforms;d.modRspViewports=d.rspViewports;
            auto vm=matrix(view(eye)),pm=matrix(projection(fov,2,100000));
            for(auto f:pairs)for(uint32_t p=0;p<w.fbPairs[f].projectionCount;p++) {
                const auto& proj=w.fbPairs[f].projections[p];if(proj.type!=Projection::Type::Perspective)continue;
                auto index=proj.transformsIndex;
                interop::float4x4 viewMatrix=vm;
                if(cinematic) {
                    const auto& authored=index<savedView.size()?savedView[index]:d.viewTransforms[index];
                    viewMatrix=hlslpp::mul(authored,vm);
                    if(target==0&&std::getenv("SNAP_VR_CINEMA_DIAG")) {
                        static unsigned samples=0;
                        if(samples++<90)fprintf(stderr,"[SNAP-VR-CINEMA] alpha %.4f view %.6f %.6f %.6f world %zu\n",worldWeight,float(authored[3][0]),float(authored[3][1]),float(authored[3][2]),d.lerpWorldTransforms.size());
                    }
                }
                d.modViewTransforms[index]=viewMatrix;d.modProjTransforms[index]=pm;d.modViewProjTransforms[index]=hlslpp::mul(viewMatrix,pm);
                auto& viewport=d.modRspViewports[index];viewport.scale.x=160;viewport.scale.y=120;viewport.translate.x=160;viewport.translate.y=120;
                // Match the native accessory depth mapping exactly. The N64
                // viewport's 511/1024 scale compresses the world depth range.
                viewport.scale.z=.5f;viewport.translate.z=.5f;
            }
            std::vector<BufferUploader::Upload> uploads;
            uploads.push_back({d.modViewProjTransforms.data(),{0,d.modViewProjTransforms.size()},sizeof(interop::float4x4),RenderBufferFlag::STORAGE,{},&w.drawBuffers.viewProjTransformsBuffer});
            uploads.push_back({d.modRspViewports.data(),{0,d.modRspViewports.size()},sizeof(interop::RSPViewport),RenderBufferFlag::STORAGE,{},&w.drawBuffers.rspViewportsBuffer});
            // Every live view, including the handheld screen, uses the current
            // tracked pose and this subframe's matched world/vertex motion.
            // Photo capture and scoring use the original game pass, not these
            // private replay targets.
            auto& transforms=(d.lerpWorldTransforms.size()==d.worldTransforms.size())?d.lerpWorldTransforms:d.worldTransforms;
            uploads.push_back({transforms.data(),{0,transforms.size()},sizeof(interop::float4x4),RenderBufferFlag::STORAGE,{},&w.drawBuffers.worldTransformsBuffer});
            upload->submit(worker,uploads);
            w.resetRSPOutputBuffers();RSPProcessor::ProcessParams rp;rp.worker=worker;rp.drawData=&d;rp.drawBuffers=&w.drawBuffers;rp.outputBuffers=&w.outputBuffers;rp.snapVRView=true;rp.curFrameWeight=worldWeight;rp.prevFrameWeight=1-rp.curFrameWeight;rsp->process(rp);
            queue.ext.textureCache->incrementLock();
            struct TextureGuard{TextureCache* cache;~TextureGuard(){cache->decrementLock();}}guard{queue.ext.textureCache};
            renderer->updateTextureCache(queue.ext.textureCache);renderer->resetFramebuffers(worker,false,w.extended.ditherNoiseStrength,RenderMultisampling{});
            for(auto f:pairs) {
                FramebufferRenderer::DrawParams p{};p.worker=worker;p.fbStorage=&framebuffers[target];p.curWorkload=&w;p.fbPairIndex=f;
                p.fbWidth=320;p.fbHeight=240;p.targetWidth=color.width;p.targetHeight=color.height;p.resolutionScale={float(color.width)/320,float(color.height)/240};
                p.aspectRatioSource=p.aspectRatioTarget=4.f/3;p.extAspectPercentage=1;p.rasterShaderCache=queue.ext.rasterShaderCache;
                p.presetScene=frame.presetScene;p.submissionFrame=w.submissionFrame;p.deltaTimeMs=1000.f/30;p.ubershadersOnly=true;p.maxGameCall=UINT32_MAX;p.snapRectWeight=1;p.snapVRWorldOnly=true;p.snapVRHighPrecisionDepth=true;
                renderer->addFramebuffer(p);
            }
            if(!d.gpuTiles.empty()) {
                renderer->createGPUTiles(d.callTiles.data(),uint32_t(d.callTiles.size()),d.gpuTiles.data(),&queue.ext.sharedResources->framebufferManager,queue.ext.textureCache,w.submissionFrame);
                queue.ext.workloadTilesUploader->submit(worker,{{d.gpuTiles.data(),{0,d.gpuTiles.size()},sizeof(interop::GPUTile),RenderBufferFlag::STORAGE,{},&w.drawBuffers.gpuTilesBuffer}});
            }
            worker->commandList->begin();renderer->endFramebuffers(worker,&w.drawBuffers,&w.outputBuffers,false);
            std::vector<BufferUploader*> uploaders{upload.get()};if(!d.gpuTiles.empty())uploaders.push_back(queue.ext.workloadTilesUploader);
            renderer->recordSetup(worker,uploaders,rsp.get(),nullptr,&w.outputBuffers,false);
            for(uint32_t f=0;f<pairs.size();f++)renderer->recordFramebuffer(worker,f);
            worker->commandList->end();renderer->waitForUploaders();worker->execute();waitWorker();renderer->advanceFrame(false);

        }
    }
    RenderTarget* desktop(GameFrame& frame) {
        RenderTarget* result=nullptr;
        for(uint32_t wi:frame.workloads){auto& w=queue.workloads[wi];for(uint32_t f=0;f<w.fbPairCount;f++) {
            const auto& p=w.fbPairs[f];if(p.colorImage.width!=320||!p.earlyPresentCandidate()||p.displayColorRect(false).height(false,true)<220)continue;
            RenderTargetKey key(p.colorImage.address,p.colorImage.width,p.colorImage.siz,Framebuffer::Type::Color);
            auto& target=queue.ext.sharedResources->renderTargetManager.get(key);if(!target.isEmpty())result=&target;
        }}return result;
    }
    void render(GameFrame& frame,const GameFrame& previous,float weight,RenderTarget* presented) {
        worldWeight=previous.matched?weight:1.f;
        Tracking t;
        if(preview) {
            t.focused=t.headValid=true;t.frame=++previewFrame;t.seconds=previewFrame/90.0;t.head.position={0,1.2f,0};
            for(unsigned i=0;i<2;i++){t.eyes[i]=t.head;t.eyes[i].position.x=i?.032f:-.032f;t.hands[i].tracked=true;t.hands[i].grip.position={i?.28f:-.28f,.95f,-.35f};}
            if(stereoTest())for(unsigned i=0;i<2;i++)t.fovs[i]=i?Fov{-.70f,.90f,.83f,-.76f}:Fov{-.90f,.70f,.83f,-.76f};
            if(std::getenv("SNAP_VR_NAME_TEST")) {
                auto& hand=t.hands[1];
                if(snap::g_scene_overlay_rom.load()==0xA5CC50u) {
                    ++nameTestFrame;float x=25,y=22;
                    if(nameTestFrame>=80&&nameTestFrame<180)y=72; // Z
                    if(nameTestFrame>=180){x=nameTestFrame<230?38.f:63.f;y=212;}
                    hand.aim.position={(x/160-1)*.8f,1.2f+(1-y/120)*.6f,0};
                    for(unsigned press:{50u,100u,200u,250u})hand.trigger=std::max(hand.trigger,nameTestFrame>=press&&nameTestFrame<press+4?1.f:0.f);
                    hand.secondary=nameTestFrame>=150&&nameTestFrame<154;
                }else if(nameTestFrame==0)hand.trigger=t.frame>120&&t.frame%70<4?1.f:0.f;
            }
            if(const char* script=std::getenv("SNAP_VR_POINTER_TEST")) {
                std::ifstream in(script);float px,py,trigger=0,back=0;
                if(in>>px>>py>>trigger>>back){auto& hand=t.hands[1];hand.aim.position={(px/160-1)*.8f,1.2f+(1-py/120)*.6f,0};hand.trigger=trigger;hand.secondary=back>0;}
            }
            if(std::getenv("SNAP_VR_PREVIEW_OPTIONS"))t.hands[0].stickClick=t.frame==20;
        }else if(!xr.begin(t)) {
            auto& s=shared();std::lock_guard lock(s.mutex);s.buttons=0;s.releases.clear();
            if(wasFocused&&s.game.course&&!s.game.paused)s.pulses|=0x1000;
            wasFocused=false;
            if(xr.needsRestart())throw std::runtime_error("OpenXR session lost; reconnecting");
            return;
        }
        wasFocused=t.focused;
        if(!preview) {
            const unsigned rate=xr.refreshRate();
            if(displayRate.exchange(rate)!=rate)
                fprintf(stderr,"[SNAP-VR] OpenXR application cadence changed to %u Hz; interpolation follows runtime pacing\n",rate);
        }
        struct FrameGuard{OpenXR& xr;bool done=false;~FrameGuard(){if(!done){try{xr.end(false);}catch(...){}}}}frameGuard{xr};
        auto start=std::chrono::steady_clock::now();GameState g;bool recenter=false,focus=false;
        {auto& s=shared();std::lock_guard lock(s.mutex);g=s.game;recenter=s.recenter;s.recenter=false;focus=false;
            if(!frame.workloads.empty()) {
                const auto& source=queue.workloads[frame.workloads.back()];
                focus=s.focusVisible&&source.snapVREpoch==g.epoch&&source.snapVRFrame==s.focusFrame;
                auto snapshot=[&](const GameFrame& renderFrame,GameState& result) {
                    if(renderFrame.workloads.empty())return false;
                    const auto& w=queue.workloads[renderFrame.workloads.back()];
                    for(auto it=s.gameHistory.rbegin();it!=s.gameHistory.rend();++it)if(it->epoch==w.snapVREpoch&&it->frame==w.snapVRFrame){result=*it;return true;}
                    return false;
                };
                GameState currentState,previousState;
                if(snapshot(frame,currentState)&&currentState.epoch==g.epoch) {
                    // Live pause and interaction state remain authoritative.
                    auto interpolated=currentState;
                    if(previous.matched&&snapshot(previous,previousState)) {
                        auto delta=frame.snapOriginDelta;
                        interpolated=interpolateCart(previousState,currentState,worldWeight,frame.snapRebaseUsable()?Vec3{float(delta.x),float(delta.y),float(delta.z)}:Vec3{});
                    }
                    g.cartPosition=interpolated.cartPosition;g.cartYaw=interpolated.cartYaw;
                }
            }}
        if(preview&&g.course&&!g.cinematic&&std::getenv("SNAP_VR_MODEL_TEST")) {
            ++modelTestFrame;auto& hand=t.hands[std::strcmp(std::getenv("SNAP_VR_MODEL_TEST"),"left")==0?0:1];
            hand.grip.position=modelTestFrame<12?Interaction::cameraDock:Vec3{std::strcmp(std::getenv("SNAP_VR_MODEL_TEST"),"left")==0?-.15f:.15f,1.15f,-.32f};
            hand.grip.orientation={.70710678f,0,0,.70710678f};
            hand.aim.orientation={.70710678f,0,0,.70710678f};hand.squeeze=modelTestFrame>=5?1.f:0.f;
            if(std::getenv("SNAP_VR_GRIP_TEST"))hand.trigger=(modelTestFrame/180)%2?1.f:0.f;
            if(std::getenv("SNAP_VR_LATENCY_TEST"))hand.trigger=modelTestFrame>180&&modelTestFrame%180<12?1.f:0.f;
        }
        if(preview&&g.course&&!g.cinematic&&modelTestFrame>500&&std::getenv("SNAP_VR_REAR_TEST")) {
            t.head.orientation=yaw(pi);
            for(unsigned i=0;i<2;i++){t.eyes[i].orientation=t.head.orientation;t.eyes[i].position=t.head.position+rotate(t.head.orientation,{i?.032f:-.032f,0,0});}
        }
        if(preview&&g.course&&std::getenv("SNAP_VR_ITEM_GRIP_TEST"))for(unsigned i=0;i<2;i++) {
            t.hands[i].grip.position={i?.18f:-.18f,1.10f,-.36f};
            t.hands[i].grip.orientation={.70710678f,0,0,.70710678f};
        }
        if(preview&&g.course&&!g.cinematic&&std::getenv("SNAP_VR_FLUTE_TEST")) {
            ++fluteTestFrame;
            t.head.orientation={-.29552f,0,0,.9553365f};
            for(unsigned i=0;i<2;i++){t.eyes[i].orientation=t.head.orientation;t.eyes[i].position=t.head.position+rotate(t.head.orientation,{i?.032f:-.032f,0,0});}
            auto& hand=t.hands[0];hand.grip.position=Interaction::fluteButton;
            unsigned phase=fluteTestFrame<100?unsigned(fluteTestFrame):unsigned(fluteTestFrame-100);
            float height=.34f;
            if(fluteTestFrame<200) {
                if(phase>=30&&phase<60)height=.34f-.18f*float(phase-30)/30;
                else if(phase>=60&&phase<75)height=.16f;
                else if(phase>=75&&phase<95)height=.16f+.18f*float(phase-75)/20;
            }
            hand.grip.position.y+=height;
            hand.squeeze=hand.trigger=0;
        }
        bool bothClicks=t.hands[0].tracked&&t.hands[1].tracked&&t.hands[0].stickClick&&t.hands[1].stickClick;
        if(recenter||(bothClicks&&!recenterDown))interaction.recenter(t);
        recenterDown=bothClicks;
        bool openedOptions=options.update(t,interaction);
        if(options.recenterRequested)interaction.recenter(t);
        auto interactionGame=g;if(options.open)interactionGame.paused=true;
        auto contacts=g.course&&!g.cinematic?props->handContacts(t,interaction):std::array<FluteContact,2>{};
        auto f=interaction.update(t,interactionGame,contacts);
        if(preview&&g.course&&std::getenv("SNAP_VR_ITEM_GRIP_TEST")){f.held[0]=Held::Apple;f.held[1]=Held::PesterBall;}
        if(g.course&&std::getenv("SNAP_VR_DIAG")&&diagnosticFrames++<90) {
            size_t interpolated=0,modified=0;
            for(auto wi:frame.workloads){auto& d=queue.workloads[wi].drawData;interpolated+=d.lerpWorldTransforms.size();modified+=d.modifyPosUints.size();}
            fprintf(stderr,"[SNAP-VR-DIAG] tick %llu alpha %.4f cart %.3f %.3f %.3f lerp %zu modified %zu message [%s]\n",(unsigned long long)g.frame,worldWeight,g.cartPosition.x,g.cartPosition.y,g.cartPosition.z,interpolated,modified,g.message.c_str());
        }
        f.pause=f.pause||(openedOptions&&g.course&&!g.paused);
        if(!preview&&t.focused)for(unsigned i=0;i<2;i++) {
            if((f.held[i]!=Held::None&&f.held[i]!=previousHeld[i])||(f.shutter&&f.held[i]==Held::Camera))xr.haptic(i);
        }
        previousHeld=f.held;
        {auto& s=shared();std::lock_guard lock(s.mutex);s.tracking=t;s.interaction=f;
            Vec3 headForward=rotate(t.head.orientation,{0,0,-1}),lensForward=rotate(f.lensInCart.orientation,{0,0,-1});
            bool valid=t.focused&&t.headValid&&g.course&&!g.paused;
            if(valid&&hadTracking&&(length(headForward-previousHeadForward)>.002f||(f.cameraHeld&&length(lensForward-previousLensForward)>.002f)))s.viewTurned=true;
            previousHeadForward=headForward;previousLensForward=lensForward;hadTracking=valid;
            s.buttons=0;s.stickX=s.stickY=0;s.namePointer={};s.menuPointer={};
            bool nameEntry=snap::g_scene_overlay_rom.load()==0xA5CC50u;
            if(t.focused&&t.headValid&&!options.open) {
                if(g.course&&!g.paused&&!g.cinematic) {
                    if(f.cameraHeld)s.buttons|=0x2000;
                    if(f.shutter){s.pulses|=0x8000;s.shutterTime=std::chrono::steady_clock::now();s.shutterPollTime={};++s.shutterSerial;}
                    if(f.dash)s.buttons|=0x10;
                    if(f.advance&&g.messageContinue)s.pulses|=0x8000;
                    if(f.flute)s.fluteRequest=true;
                    for(auto release:f.throws)if(s.releases.size()<2)s.releases.push_back(release);
                }else {
                    unsigned hand=interaction.settings.leftHanded?0:1;
                    s.stickX=t.hands[hand].tracked?t.hands[hand].stickX:0;s.stickY=t.hands[hand].tracked?t.hands[hand].stickY:0;
                    // Map the ray to the same 320x240 coordinates as the menu sprites.
                    if(t.hands[hand].tracked&&std::abs(s.stickX)<.15f&&std::abs(s.stickY)<.15f) {
                        Pose aim=interaction.localPose(t.hands[hand].aim);Vec3 ray=rotate(aim.orientation,{0,0,-1});
                        if(ray.z<-.01f) {
                            float distance=(-1.6f-aim.position.z)/ray.z;Vec3 hit=aim.position+ray*distance;
                            if(distance>0&&std::abs(hit.x)<.8f&&std::abs(hit.y-interaction.settings.eyeHeight)<.6f) {
                                float dx=hit.x/.8f,dy=(hit.y-interaction.settings.eyeHeight)/.6f;
                                s.menuPointer={(dx+1)*160,(1-dy)*120,true};
                                if(nameEntry)s.namePointer=nameCell(s.menuPointer.x,s.menuPointer.y);
                            }
                        }
                    }
                    if(t.hands[hand].tracked) {
                        bool confirm=t.hands[hand].trigger>.6f||t.hands[hand].primary;
                        if(confirm)s.buttons|=0x8000;
                        if(confirm&&!menuConfirmDown) {
                            s.pulses|=0x8000;
                            s.nameClick=nameEntry?s.namePointer:NameCell{};
                            s.menuClick=s.menuPointer;++s.menuClickSerial;
                        }
                        menuConfirmDown=confirm;
                        if(t.hands[hand].secondary)s.buttons|=0x4000;
                    }else menuConfirmDown=false;
                    if(t.hands[0].tracked&&t.hands[0].menu)s.buttons|=0x1000;
                }
            } else {s.releases.clear();menuConfirmDown=false;s.nameClick={};}
            if(f.pause)s.pulses|=0x1000;
        }
        if((!preview&&!xr.shouldRender())||!t.headValid){xr.end(false);frameGuard.done=true;return;}
        workerWaitMs=0;props->beginTiming();
        auto* worker=queue.ext.workloadGraphicsWorker;RenderTarget* screen=nullptr;
        bool cinema=g.cinematic&&!g.paused&&!options.open;
        bool course=g.course&&g.frame>0&&!g.paused&&!options.open&&!cinema;
        int desiredView=cinema?2:course?1:0;
        if(desiredView!=transition.target&&std::getenv("SNAP_VR_TRANSITION_DIAG"))fprintf(stderr,"[SNAP-VR-VIEW] frame %llu epoch %llu view %d -> %d\n",(unsigned long long)t.frame,(unsigned long long)g.epoch,transition.target,desiredView);
        auto fade=transition.update(desiredView,t.seconds);
        if(course) {
            float vertical=f.fovY*pi/360;float horizontal=std::atan(std::tan(vertical)*4/3);
            replay(frame,2,f.lens,{-horizontal,horizontal,vertical,-vertical},true);screen=colors[2].get();
            // Bounded, preview-only capture of successive live viewfinder
            // samples. Readback stalls make this unsuitable for timing tests.
            if(preview&&modelTestFrame>120&&viewfinderTestFrames<12&&std::getenv("SNAP_VR_VIEWFINDER_TEST")) {
                const std::string filename="vr-viewfinder-"+std::to_string(viewfinderTestFrames++)+".png";
                props->capture(static_cast<plume::D3D12Texture*>(screen->texture.get())->d3d,filename.c_str());
                const auto& source=queue.workloads[frame.workloads.back()];
                fprintf(stderr,"[SNAP-VR-VIEWFINDER] %s workload %llu alpha %.4f lens %.3f %.3f %.3f\n",
                    filename.c_str(),(unsigned long long)source.workloadId,worldWeight,f.lens.position.x,f.lens.position.y,f.lens.position.z);
            }
        }else {
            screen=presented?presented:desktop(frame);
            // Keep a private copy: score readbacks may render several scratch
            // tasks without a new display image. Continue showing the last UI
            // with current head tracking instead of submitting an empty layer.
            if(screen&&!screen->isEmpty()) {
                if(!menuImage||menuImage->usesHDR!=screen->usesHDR)
                    menuImage=std::make_unique<RenderTarget>(0,Framebuffer::Type::Color,RenderMultisampling{},screen->usesHDR);
                menuImage->resize(worker,screen->width,screen->height);
                worker->commandList->begin();
                if(screen->multisampling.sampleCount>1)menuImage->resolveFromTarget(worker,screen,queue.ext.shaderLibrary);
                else menuImage->snapCopyFromTargetRaster(worker,screen,queue.ext.shaderLibrary);
                worker->commandList->end();worker->execute();waitWorker();
            }
            screen=menuImage.get();
        }
        if(!screen){xr.end(false);frameGuard.done=true;return;}
        const bool captureSubmitted=!preview&&std::filesystem::exists("vr-capture.request");
        for(unsigned i=0;i<2;i++) {
            auto native=[](RenderTarget* target){return static_cast<plume::D3D12Texture*>(target->texture.get())->d3d;};
            if(!fade.hold) {
            Pose eye=interaction.toWorld(t.eyes[i],g);
            if(cinema){eye=interaction.localPose(t.eyes[i]);eye.position.y-=interaction.settings.eyeHeight;eye.position=eye.position*interaction.settings.unitsPerMeter;}
            replay(frame,i,eye,t.fovs[i],course||cinema,cinema);
            worker->commandList->begin();
            worker->commandList->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(screen->texture.get(),RenderTextureLayout::SHADER_READ));
            worker->commandList->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(colors[i]->texture.get(),RenderTextureLayout::COLOR_WRITE));
            worker->commandList->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(depths[i]->texture.get(),RenderTextureLayout::DEPTH_WRITE));
            worker->commandList->end();worker->execute();waitWorker();
            auto displayGame=g;displayGame.course=course;
            if(!cinema)props->draw(native(colors[i].get()),native(depths[i].get()),native(screen),course?eye:t.eyes[i],t.fovs[i],displayGame,f,t,interaction,focus,options.open,options.row);
            }
            if(fade.gain<1||(!fade.hold&&cinema))props->presentation(native(colors[i].get()),interaction.localPose(t.eyes[i]),t.fovs[i],fade.gain,!fade.hold&&cinema,interaction.settings.eyeHeight);
            if(!preview){
                auto* image=xr.acquire(i);props->copy(native(colors[i].get()),image,width(i),height(i));
                if(captureSubmitted)props->capture(image,i?"vr-submitted-right.png":"vr-submitted-left.png");
                xr.release(i);
            }
            if(!preview&&!capturedHeadset&&t.focused&&t.frame>=120&&std::getenv("SNAP_VR_CAPTURE")) {
                props->capture(native(colors[i].get()),i?"vr-headset-right.png":"vr-headset-left.png");
                if(i==1)capturedHeadset=true;
            }
            if(preview&&(previewFrame==30||previewFrame%120==0||(previewFrame%5==0&&std::getenv("SNAP_VR_TUTORIAL_TEST")&&snap::g_scene_overlay_rom.load()==0x8A70E0u)||(std::getenv("SNAP_VR_TRANSITION_DIAG")&&(fade.hold||fade.gain<1)&&previewFrame%5==0))) {
                std::string filename="vr-preview-"+std::to_string(previewFrame)+(i?"-right.png":"-left.png");props->capture(native(colors[i].get()),filename.c_str());
            }
        }
        if(captureSubmitted){std::error_code error;std::filesystem::remove("vr-capture.request",error);}
        accumulatedGpuMs+=props->endTiming();accumulatedWaitMs+=workerWaitMs+props->fenceWaitMs();
        xr.end(true);frameGuard.done=true;
        const auto submitted=std::chrono::steady_clock::now();
        if(submissionWindow==std::chrono::steady_clock::time_point{})submissionWindow=submitted;
        else if(++submissionIntervals==120) {
            const double seconds=std::chrono::duration<double>(submitted-submissionWindow).count();
            fprintf(stderr,"[SNAP-VR] %.1f submitted FPS, %u Hz interpolation target (%s; includes game pass and pacing)\n",
                submissionIntervals/seconds,displayRate.load(),preview?"synthetic preview":"OpenXR application cadence");
            submissionWindow=submitted;submissionIntervals=0;
        }
        accumulatedMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        if(++timedFrames%120==0){
            fprintf(stderr,"[SNAP-VR] %u Hz, %ux%u: VR CPU %.2f ms, GPU queue span %.2f ms, fence waits %.2f ms (120-frame means; excludes original game pass)\n",
                displayRate.load(),width(0),height(0),accumulatedMs/120,accumulatedGpuMs/120,accumulatedWaitMs/120);
            accumulatedMs=accumulatedGpuMs=accumulatedWaitMs=0;
        }
    }
};
std::unique_ptr<Renderer> renderer;
auto retryAfter=std::chrono::steady_clock::time_point{};
}
void render(RT64::WorkloadQueue& queue,RT64::GameFrame& frame,const RT64::GameFrame& previous,float weight,RenderTarget* presented) {
    if(!requested.load()||std::chrono::steady_clock::now()<retryAfter)return;
    try {
        std::scoped_lock lock(queue.ext.sharedResources->workloadMutex,queue.workerMutex);
        if(!renderer)renderer=std::make_unique<Renderer>(queue);
        renderer->render(frame,previous,weight,presented);
    }catch(const std::exception& e){
        fprintf(stderr,"[SNAP-VR] %s; retrying in two seconds\n",e.what());
        auto& state=shared();{std::lock_guard lock(state.mutex);state.buttons=0;state.releases.clear();
            if(state.game.course&&!state.game.paused)state.pulses|=0x1000;}
        renderer.reset();retryAfter=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    }
}
void shutdown(){renderer.reset();retryAfter={};}
}
