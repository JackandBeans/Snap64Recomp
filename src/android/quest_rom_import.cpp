#include "vr/vr_openxr.h"
#include "vr/vr_props.h"
#include <SDL.h>
#include <SDL_system.h>
#include <jni.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
using namespace snap::vr;
using namespace plume;
struct JavaImport {
    JNIEnv* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject activity=static_cast<jobject>(SDL_AndroidGetActivity());
    jclass type=env->GetObjectClass(activity);
    jmethodID stateMethod=env->GetMethodID(type,"romImportState","()I");
    jmethodID progressMethod=env->GetMethodID(type,"romImportProgress","()I");
    jmethodID messageMethod=env->GetMethodID(type,"romImportMessage","()Ljava/lang/String;");
    jmethodID chooseMethod=env->GetMethodID(type,"chooseRom","()V");
    ~JavaImport(){env->DeleteLocalRef(type);env->DeleteLocalRef(activity);}
    void check(){if(env->ExceptionCheck()){env->ExceptionDescribe();env->ExceptionClear();throw std::runtime_error("ROM import bridge failed");}}
    int state(){int value=env->CallIntMethod(activity,stateMethod);check();return value;}
    int progress(){int value=env->CallIntMethod(activity,progressMethod);check();return value;}
    std::string message(){
        auto value=static_cast<jstring>(env->CallObjectMethod(activity,messageMethod));check();
        if(!value)return {};
        const char* chars=env->GetStringUTFChars(value,nullptr);std::string text=chars?chars:"";
        if(chars)env->ReleaseStringUTFChars(value,chars);env->DeleteLocalRef(value);return text;
    }
    void choose(){env->CallVoidMethod(activity,chooseMethod);check();}
};
bool pointerHit(Pose ray,Pose panel,Vec3& point) {
    auto local=compose(inverse(panel),ray);auto direction=rotate(local.orientation,{0,0,-1});
    if(direction.z>=-.001f)return false;
    float distance=-local.position.z/direction.z;if(distance<=0)return false;
    point=local.position+direction*distance;
    return std::abs(point.x)<1.08f&&std::abs(point.y)<.67f;
}
bool okayHit(Vec3 point){return std::abs(point.x)<=.22f&&point.y>=-.535f&&point.y<=-.385f;}
}

// A self-contained tracked OpenXR session before any guest ROM/game state exists.
// The splash session and GPU resources are fully retired before the game creates
// its normal session. No game workload or desktop-preview renderer is involved.
bool snap_quest_import_rom() {
    JavaImport importer;
    while(importer.state()==0)SDL_Delay(1);
    if(importer.state()==4)return true;
    if(SDL_InitSubSystem(SDL_INIT_EVENTS)!=0)throw std::runtime_error(SDL_GetError());
    struct Events {~Events(){SDL_QuitSubSystem(SDL_INIT_EVENTS);}} events;
    auto graphics=std::make_unique<VulkanInterface>();
    if(!graphics->isValid())throw std::runtime_error("Unable to create ROM setup Vulkan interface");
    auto device=graphics->createDevice("");
    if(!device)throw std::runtime_error("Unable to create ROM setup graphics device");
    auto queue=device->createCommandQueue(RenderCommandListType::DIRECT);
    auto* nativeDevice=static_cast<VRDevice*>(device.get());
    auto* nativeQueue=static_cast<VRQueue*>(queue.get());
    OpenXR xr;std::string error;
    if(!xr.initialize(nativeDevice,nativeQueue,1.f,error))throw std::runtime_error(error);
    std::array<std::unique_ptr<RenderTexture>,2> colors,depths;
    for(unsigned i=0;i<2;i++) {
        colors[i]=device->createTexture(RenderTextureDesc::Texture2D(xr.width(i),xr.height(i),1,RenderFormat::R8G8B8A8_UNORM,RenderTextureFlag::RENDER_TARGET));
        depths[i]=device->createTexture(RenderTextureDesc::Texture2D(xr.width(i),xr.height(i),1,RenderFormat::D32_FLOAT,RenderTextureFlag::DEPTH_TARGET));
    }
    Props props(nativeDevice,nativeQueue);
    Pose panel;bool anchored=false,pressed=false;double dwellStart=-1;
    int previousState=-1;
    for(;;) {
        SDL_Event event;while(SDL_PollEvent(&event))if(event.type==SDL_QUIT)return false;
        int state=importer.state();
        if(state!=previousState){std::fprintf(stderr,"[SNAP-ROM] Immersive import state %d\n",state);previousState=state;}
        if(state==4){std::fprintf(stderr,"[SNAP-ROM] Import verified; starting normal game\n");return true;}
        Tracking tracking;
        if(!xr.begin(tracking)){if(xr.needsRestart())return false;SDL_Delay(10);continue;}
        if(!xr.shouldRender()||!tracking.headValid){xr.end(false);continue;}
        if(!anchored) {
            // Face the initial user pose and remain spatially anchored.
            panel=tracking.head;panel.position=tracking.head.position+rotate(tracking.head.orientation,{0,0,-2.2f});
            anchored=true;
        }
        Vec3 point{};bool hasPointer=false,hover=false,down=false,controller=false;
        for(const auto& hand:tracking.hands)if(hand.tracked) {
            Vec3 hit;
            if(pointerHit(hand.aim,panel,hit)) {
                if(!hasPointer||okayHit(hit)){point=hit;hasPointer=true;hover=okayHit(hit);}
                down|=okayHit(hit)&&(hand.trigger>.6f||hand.primary);controller=true;
            }
        }
        if(!controller){hasPointer=pointerHit(tracking.head,panel,point);hover=hasPointer&&okayHit(point);}
        bool available=tracking.focused&&(state==1||state==5);
        if(available&&hover&&!controller) {
            if(dwellStart<0)dwellStart=tracking.seconds;
        } else dwellStart=-1;
        bool confirm=available&&((down&&!pressed)||(dwellStart>=0&&tracking.seconds-dwellStart>=1.4));
        pressed=down;
#ifdef SNAP_QUEST_BENCHMARK
        if(available&&std::filesystem::exists("benchmark/import-confirm.request")) {
            std::filesystem::remove("benchmark/import-confirm.request");confirm=true;
        }
        const bool capture=std::filesystem::exists("benchmark/import-capture.request");
#else
        constexpr bool capture=false;
#endif
        props.beginTiming();
        for(unsigned i=0;i<2;i++) {
            props.importSplash(static_cast<VRTexture*>(colors[i].get()),static_cast<VRTexture*>(depths[i].get()),
                tracking.eyes[i],tracking.fovs[i],panel,tracking.frame,state,importer.progress(),importer.message(),
                hover&&available,point,hasPointer&&available);
            auto* image=xr.acquire(i);props.copy(static_cast<VRTexture*>(colors[i].get()),image,xr.width(i),xr.height(i));
#ifdef SNAP_QUEST_BENCHMARK
            if(capture)props.capture(image,i?"benchmark/import-right.png":"benchmark/import-left.png");
#endif
        }
        // One completion fence covers both eyes before release and reuse.
        props.endTiming();xr.end(true);
#ifdef SNAP_QUEST_BENCHMARK
        if(capture)std::filesystem::remove("benchmark/import-capture.request");
        if(tracking.frame%90==0) {
            std::ofstream("benchmark/import-status.json")<<"{\"state\":"<<state<<",\"frame\":"<<tracking.frame
                <<",\"focused\":"<<(tracking.focused?"true":"false")<<",\"width\":"<<xr.width(0)<<",\"height\":"<<xr.height(0)<<"}";
        }
#endif
        if(confirm){importer.choose();dwellStart=-1;pressed=true;}
    }
}
