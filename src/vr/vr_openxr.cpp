#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D12
#include <windows.h>
#include <d3d12.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "vr_openxr.h"
#include <cstring>
#include <cstdio>
#include <stdexcept>

namespace snap::vr {
namespace {
void check(XrResult r,const char* op) { if(XR_FAILED(r)) throw std::runtime_error(std::string(op)+" failed ("+std::to_string(r)+")"); }
Pose pose(XrPosef p) { return {{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w},{p.position.x,p.position.y,p.position.z}}; }
bool valid(XrSpaceLocationFlags f) { return (f&(XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))==(XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT); }
}
struct OpenXR::Impl {
    XrInstance instance=XR_NULL_HANDLE;
    XrSession session=XR_NULL_HANDLE;
    XrSpace local=XR_NULL_HANDLE,head=XR_NULL_HANDLE;
    XrActionSet actions=XR_NULL_HANDLE;
    XrAction gripPose{},aimPose{},squeeze{},trigger{},stick{},primary{},secondary{},menu{},thumb{},stickClick{},vibration{};
    std::array<XrPath,2> hands{};
    std::array<XrSpace,2> grips{},aims{};
    std::array<XrSwapchain,2> chains{};
    std::array<std::vector<XrSwapchainImageD3D12KHR>,2> images;
    std::array<XrViewConfigurationView,2> config{};
    std::array<XrView,2> views{};
    std::array<uint32_t,2> acquired{};
    std::array<bool,2> held{};
    XrSessionState state=XR_SESSION_STATE_UNKNOWN;
    XrFrameState frame{XR_TYPE_FRAME_STATE};
    bool running=false,begun=false,lost=false;
    uint64_t frameId=0;
    int64_t colorFormat=DXGI_FORMAT_R8G8B8A8_UNORM;
    ~Impl() {
        if(session) {
            for(unsigned i=0;i<2;i++) {if(grips[i])xrDestroySpace(grips[i]);if(aims[i])xrDestroySpace(aims[i]);if(chains[i])xrDestroySwapchain(chains[i]);}
            if(head)xrDestroySpace(head);if(local)xrDestroySpace(local);xrDestroySession(session);
        }
        if(actions)xrDestroyActionSet(actions);
        if(instance)xrDestroyInstance(instance);
    }
    XrPath path(const char* s) {XrPath p;check(xrStringToPath(instance,s,&p),"xrStringToPath");return p;}
    XrAction action(const char* name,XrActionType type) {
        XrActionCreateInfo ci{XR_TYPE_ACTION_CREATE_INFO};
        std::strncpy(ci.actionName,name,sizeof(ci.actionName)-1);std::strncpy(ci.localizedActionName,name,sizeof(ci.localizedActionName)-1);
        ci.actionType=type;ci.countSubactionPaths=2;ci.subactionPaths=hands.data();
        XrAction a;check(xrCreateAction(actions,&ci,&a),"xrCreateAction");return a;
    }
    XrActionStateGetInfo info(XrAction a,unsigned i) {XrActionStateGetInfo g{XR_TYPE_ACTION_STATE_GET_INFO};g.action=a;g.subactionPath=hands[i];return g;}
    float scalar(XrAction a,unsigned i) {auto g=info(a,i);XrActionStateFloat s{XR_TYPE_ACTION_STATE_FLOAT};check(xrGetActionStateFloat(session,&g,&s),"float action");return s.isActive?s.currentState:0;}
    bool boolean(XrAction a,unsigned i) {auto g=info(a,i);XrActionStateBoolean s{XR_TYPE_ACTION_STATE_BOOLEAN};check(xrGetActionStateBoolean(session,&g,&s),"boolean action");return s.isActive&&s.currentState;}
};
OpenXR::OpenXR():impl(std::make_unique<Impl>()) {}
OpenXR::~OpenXR()=default;
bool OpenXR::initialize(ID3D12Device* device,ID3D12CommandQueue* queue,float scale,std::string& error) {
    try {
        auto& x=*impl;
        const char* extensions[]={XR_KHR_D3D12_ENABLE_EXTENSION_NAME};
        XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
        std::strcpy(ci.applicationInfo.applicationName,"Snap64 VR");ci.applicationInfo.apiVersion=XR_API_VERSION_1_0;
        ci.enabledExtensionCount=1;ci.enabledExtensionNames=extensions;
        check(xrCreateInstance(&ci,&x.instance),"xrCreateInstance");
        XrSystemGetInfo si{XR_TYPE_SYSTEM_GET_INFO};si.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        XrSystemId system;check(xrGetSystem(x.instance,&si,&system),"xrGetSystem (connect your headset)");
        PFN_xrGetD3D12GraphicsRequirementsKHR requirements=nullptr;
        check(xrGetInstanceProcAddr(x.instance,"xrGetD3D12GraphicsRequirementsKHR",reinterpret_cast<PFN_xrVoidFunction*>(&requirements)),"D3D12 requirements function");
        XrGraphicsRequirementsD3D12KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR};check(requirements(x.instance,system,&req),"D3D12 requirements");
        LUID adapter=device->GetAdapterLuid();
        if(adapter.HighPart!=req.adapterLuid.HighPart||adapter.LowPart!=req.adapterLuid.LowPart) throw std::runtime_error("OpenXR and RT64 selected different GPUs. Select the headset GPU for Snap64Recomp in Windows Graphics settings.");
        XrGraphicsBindingD3D12KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D12_KHR};binding.device=device;binding.queue=queue;
        XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};sci.next=&binding;sci.systemId=system;
        check(xrCreateSession(x.instance,&sci,&x.session),"xrCreateSession");
        XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};space.poseInReferenceSpace.orientation.w=1;
        space.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;check(xrCreateReferenceSpace(x.session,&space,&x.local),"local space");
        space.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;check(xrCreateReferenceSpace(x.session,&space,&x.head),"head space");
        x.hands={x.path("/user/hand/left"),x.path("/user/hand/right")};
        XrActionSetCreateInfo aci{XR_TYPE_ACTION_SET_CREATE_INFO};std::strcpy(aci.actionSetName,"snap");std::strcpy(aci.localizedActionSetName,"Pokemon Snap");
        check(xrCreateActionSet(x.instance,&aci,&x.actions),"action set");
        x.gripPose=x.action("grip_pose",XR_ACTION_TYPE_POSE_INPUT);x.aimPose=x.action("aim_pose",XR_ACTION_TYPE_POSE_INPUT);
        x.squeeze=x.action("squeeze",XR_ACTION_TYPE_FLOAT_INPUT);x.trigger=x.action("trigger",XR_ACTION_TYPE_FLOAT_INPUT);
        x.stick=x.action("stick",XR_ACTION_TYPE_VECTOR2F_INPUT);x.primary=x.action("primary",XR_ACTION_TYPE_BOOLEAN_INPUT);
        x.secondary=x.action("secondary",XR_ACTION_TYPE_BOOLEAN_INPUT);x.menu=x.action("menu",XR_ACTION_TYPE_BOOLEAN_INPUT);
        x.thumb=x.action("thumb_touch",XR_ACTION_TYPE_BOOLEAN_INPUT);x.vibration=x.action("haptic",XR_ACTION_TYPE_VIBRATION_OUTPUT);
        x.stickClick=x.action("stick_click",XR_ACTION_TYPE_BOOLEAN_INPUT);
        std::vector<XrActionSuggestedBinding> binds;
        for(unsigned i=0;i<2;i++) {
            std::string root=i?"/user/hand/right":"/user/hand/left";
            auto add=[&](XrAction a,const char* suffix){binds.push_back({a,x.path((root+suffix).c_str())});};
            add(x.gripPose,"/input/grip/pose");add(x.aimPose,"/input/aim/pose");add(x.squeeze,"/input/squeeze/value");
            add(x.trigger,"/input/trigger/value");add(x.stick,"/input/thumbstick");add(x.thumb,"/input/thumbstick/touch");add(x.vibration,"/output/haptic");
            add(x.stickClick,"/input/thumbstick/click");
            add(x.primary,i?"/input/a/click":"/input/x/click");add(x.secondary,i?"/input/b/click":"/input/y/click");
            if(!i)add(x.menu,"/input/menu/click");
        }
        XrInteractionProfileSuggestedBinding profile{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        profile.interactionProfile=x.path("/interaction_profiles/oculus/touch_controller");profile.countSuggestedBindings=uint32_t(binds.size());profile.suggestedBindings=binds.data();
        check(xrSuggestInteractionProfileBindings(x.instance,&profile),"Touch bindings");
        XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};attach.countActionSets=1;attach.actionSets=&x.actions;
        check(xrAttachSessionActionSets(x.session,&attach),"attach actions");
        for(unsigned i=0;i<2;i++) {
            XrActionSpaceCreateInfo ai{XR_TYPE_ACTION_SPACE_CREATE_INFO};ai.subactionPath=x.hands[i];ai.poseInActionSpace.orientation.w=1;
            ai.action=x.gripPose;check(xrCreateActionSpace(x.session,&ai,&x.grips[i]),"grip space");
            ai.action=x.aimPose;check(xrCreateActionSpace(x.session,&ai,&x.aims[i]),"aim space");
            x.config[i]={XR_TYPE_VIEW_CONFIGURATION_VIEW};x.views[i]={XR_TYPE_VIEW};
        }
        uint32_t count=0;check(xrEnumerateViewConfigurationViews(x.instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,x.config.data()),"stereo views");
        if(count!=2)throw std::runtime_error("Exactly two stereo views are required");
        check(xrEnumerateSwapchainFormats(x.session,0,&count,nullptr),"swapchain formats");std::vector<int64_t> formats(count);
        check(xrEnumerateSwapchainFormats(x.session,count,&count,formats.data()),"swapchain formats");
        // RGBA8 linear and sRGB are copy-compatible D3D12 format families.
        // The original game's display values are already gamma encoded.
        if(std::find(formats.begin(),formats.end(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)!=formats.end())
            x.colorFormat=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        else if(std::find(formats.begin(),formats.end(),x.colorFormat)==formats.end())
            throw std::runtime_error("Runtime exposes no copy-compatible RGBA8 swapchain format");
        for(unsigned i=0;i<2;i++) {
            auto& c=x.config[i];c.recommendedImageRectWidth=std::clamp(uint32_t(c.recommendedImageRectWidth*scale),1u,c.maxImageRectWidth);
            c.recommendedImageRectHeight=std::clamp(uint32_t(c.recommendedImageRectHeight*scale),1u,c.maxImageRectHeight);
            XrSwapchainCreateInfo sc{XR_TYPE_SWAPCHAIN_CREATE_INFO};sc.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
            sc.format=x.colorFormat;sc.sampleCount=1;sc.width=c.recommendedImageRectWidth;sc.height=c.recommendedImageRectHeight;sc.faceCount=sc.arraySize=sc.mipCount=1;
            check(xrCreateSwapchain(x.session,&sc,&x.chains[i]),"create eye swapchain");
            check(xrEnumerateSwapchainImages(x.chains[i],0,&count,nullptr),"swapchain images");x.images[i].resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
            check(xrEnumerateSwapchainImages(x.chains[i],count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(x.images[i].data())),"swapchain images");
        }
        return true;
    } catch(const std::exception& e) {error=e.what();impl=std::make_unique<Impl>();return false;}
}
bool OpenXR::begin(Tracking& t) {
    auto& x=*impl;if(!x.instance)return false;
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    while(xrPollEvent(x.instance,&event)==XR_SUCCESS) {
        if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            x.state=reinterpret_cast<XrEventDataSessionStateChanged*>(&event)->state;
            fprintf(stderr,"[SNAP-VR] OpenXR session state %d\n",int(x.state));
            if(x.state==XR_SESSION_STATE_READY&&!x.running) {XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};bi.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;check(xrBeginSession(x.session,&bi),"begin session");x.running=true;fprintf(stderr,"[SNAP-VR] session begun\n");}
            if(x.state==XR_SESSION_STATE_STOPPING&&x.running) {check(xrEndSession(x.session),"end session");x.running=false;}
            if(x.state==XR_SESSION_STATE_LOSS_PENDING||x.state==XR_SESSION_STATE_EXITING){x.running=false;x.lost=true;}
        }
        if(event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING){x.running=false;x.lost=true;}
        event={XR_TYPE_EVENT_DATA_BUFFER};
    }
    if(!x.running)return false;
    XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};check(xrWaitFrame(x.session,&wait,&x.frame),"wait frame");
    XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};check(xrBeginFrame(x.session,&bi),"begin frame");x.begun=true;
    t={};t.frame=++x.frameId;t.seconds=double(x.frame.predictedDisplayTime)*1e-9;t.focused=x.state==XR_SESSION_STATE_FOCUSED;
    XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};check(xrLocateSpace(x.head,x.local,x.frame.predictedDisplayTime,&head),"locate head");t.head=pose(head.pose);t.headValid=valid(head.locationFlags);
    XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};li.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;li.displayTime=x.frame.predictedDisplayTime;li.space=x.local;
    XrViewState vs{XR_TYPE_VIEW_STATE};uint32_t count;
    check(xrLocateViews(x.session,&li,&vs,2,&count,x.views.data()),"locate views");
    t.headValid=t.headValid&&count==2&&(vs.viewStateFlags&XR_VIEW_STATE_ORIENTATION_VALID_BIT)&&(vs.viewStateFlags&XR_VIEW_STATE_POSITION_VALID_BIT);
    XrActiveActionSet active{x.actions,XR_NULL_PATH};XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};sync.countActiveActionSets=1;sync.activeActionSets=&active;
    if(t.focused)check(xrSyncActions(x.session,&sync),"sync actions");
    for(unsigned i=0;i<2;i++) {
        t.eyes[i]=pose(x.views[i].pose);auto f=x.views[i].fov;t.fovs[i]={f.angleLeft,f.angleRight,f.angleUp,f.angleDown};
        if(!t.focused)continue;
        auto& h=t.hands[i];XrSpaceLocation gl{XR_TYPE_SPACE_LOCATION},al{XR_TYPE_SPACE_LOCATION};
        check(xrLocateSpace(x.grips[i],x.local,x.frame.predictedDisplayTime,&gl),"locate grip");check(xrLocateSpace(x.aims[i],x.local,x.frame.predictedDisplayTime,&al),"locate aim");
        constexpr XrSpaceLocationFlags tracked=XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
        auto poseInfo=x.info(x.gripPose,i);XrActionStatePose actionPose{XR_TYPE_ACTION_STATE_POSE};
        check(xrGetActionStatePose(x.session,&poseInfo,&actionPose),"pose action");
        h.tracked=actionPose.isActive&&valid(gl.locationFlags)&&valid(al.locationFlags)&&(gl.locationFlags&tracked)==tracked;h.grip=pose(gl.pose);h.aim=pose(al.pose);
        h.squeeze=x.scalar(x.squeeze,i);h.trigger=x.scalar(x.trigger,i);h.primary=x.boolean(x.primary,i);h.secondary=x.boolean(x.secondary,i);h.menu=x.boolean(x.menu,i);h.thumbTouch=x.boolean(x.thumb,i);
        h.stickClick=x.boolean(x.stickClick,i);
        auto gi=x.info(x.stick,i);XrActionStateVector2f v{XR_TYPE_ACTION_STATE_VECTOR2F};check(xrGetActionStateVector2f(x.session,&gi,&v),"thumbstick");if(v.isActive){h.stickX=v.currentState.x;h.stickY=v.currentState.y;}
    }
    return true;
}
ID3D12Resource* OpenXR::acquire(unsigned i) {
    auto& x=*impl;XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};check(xrAcquireSwapchainImage(x.chains.at(i),&ai,&x.acquired[i]),"acquire eye");x.held[i]=true;
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wi.timeout=XR_INFINITE_DURATION;check(xrWaitSwapchainImage(x.chains[i],&wi),"wait eye");return x.images[i][x.acquired[i]].texture;
}
void OpenXR::release(unsigned i) {auto& x=*impl;if(!x.held.at(i))return;XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};check(xrReleaseSwapchainImage(x.chains[i],&ri),"release eye");x.held[i]=false;}
void OpenXR::end(bool rendered) {
    auto& x=*impl;if(!x.begun)return;
    for(unsigned i=0;i<2;i++)release(i);
    std::array<XrCompositionLayerProjectionView,2> views{};
    for(unsigned i=0;i<2;i++){auto& v=views[i];v={XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};v.pose=x.views[i].pose;v.fov=x.views[i].fov;v.subImage.swapchain=x.chains[i];v.subImage.imageRect.extent={int32_t(width(i)),int32_t(height(i))};}
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};layer.space=x.local;layer.viewCount=2;layer.views=views.data();
    const XrCompositionLayerBaseHeader* layers[]={reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};
    XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};ei.displayTime=x.frame.predictedDisplayTime;ei.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;ei.layerCount=rendered&&x.frame.shouldRender?1:0;ei.layers=layers;
    check(xrEndFrame(x.session,&ei),"end frame");x.begun=false;
}
void OpenXR::haptic(unsigned i,float strength) {auto& x=*impl;XrHapticActionInfo hi{XR_TYPE_HAPTIC_ACTION_INFO};hi.action=x.vibration;hi.subactionPath=x.hands.at(i);XrHapticVibration v{XR_TYPE_HAPTIC_VIBRATION};v.amplitude=strength;v.duration=30000000;v.frequency=XR_FREQUENCY_UNSPECIFIED;xrApplyHapticFeedback(x.session,&hi,reinterpret_cast<XrHapticBaseHeader*>(&v));}
unsigned OpenXR::width(unsigned i)const{return impl->config.at(i).recommendedImageRectWidth;}
unsigned OpenXR::height(unsigned i)const{return impl->config.at(i).recommendedImageRectHeight;}
int64_t OpenXR::format()const{return impl->colorFormat;}
bool OpenXR::shouldRender()const{return impl->frame.shouldRender;}
bool OpenXR::needsRestart()const{return impl->lost;}
unsigned OpenXR::refreshRate()const {
    auto period=impl->frame.predictedDisplayPeriod;
    // This is the runtime's application cadence, which can be below the
    // physical display rate during reprojection. Forcing 45/48 Hz to 60
    // generates extra interpolation frames that xrWaitFrame cannot consume.
    return period>0?std::clamp(unsigned(std::lround(1e9/double(period))),1u,1000u):90u;
}
}
