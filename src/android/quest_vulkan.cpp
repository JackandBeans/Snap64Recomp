#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_VULKAN
#define XR_USE_TIMESPEC
#include <time.h>
#include <jni.h>
#include <android/native_window_jni.h>
#include "quest_vulkan.h"
#include "quest_xr.h"
#include <openxr/openxr_platform.h>
#include <SDL_system.h>
#include <mutex>
#include <condition_variable>
#include <stdexcept>
#include <cstring>
#include <vector>
#include <cstdlib>

namespace {
std::mutex windowMutex;
std::condition_variable windowReady;
ANativeWindow* currentWindow=nullptr;
}
extern "C" JNIEXPORT void JNICALL
Java_org_snap64_quest_QuestActivity_nativeSetSurface(JNIEnv* env,jclass,jobject surface) {
    auto* window=surface?ANativeWindow_fromSurface(env,surface):nullptr;
    std::lock_guard lock(windowMutex);
    if(currentWindow)ANativeWindow_release(currentWindow);
    currentWindow=window;
    windowReady.notify_all();
}
ANativeWindow* snap_quest_wait_window() {
    std::unique_lock lock(windowMutex);
    windowReady.wait(lock,[]{return currentWindow!=nullptr;});
    ANativeWindow_acquire(currentWindow);
    return currentWindow;
}
ANativeWindow* snap_quest_acquire_window() {
    std::lock_guard lock(windowMutex);
    if(currentWindow)ANativeWindow_acquire(currentWindow);
    return currentWindow;
}
bool snap_quest_window_changed(ANativeWindow* window) {
    std::lock_guard lock(windowMutex);
    return window!=currentWindow;
}
bool snap_quest_refresh_surface(plume::VulkanSwapChain* chain) {
    auto* window=snap_quest_acquire_window();
    if(window!=chain->desc.renderWindow) {
        // The present worker has completed its command lists before resize.
        // Retire the presentation operation before releasing the old surface.
        auto* queue=chain->commandQueue->queue;
        {
            std::lock_guard lock(*queue->mutex);
            vkQueueWaitIdle(queue->vk);
        }
        chain->releaseImageViews();
        chain->releaseSwapChain();
        auto instance=chain->commandQueue->device->renderInterface->instance;
        if(chain->surface)vkDestroySurfaceKHR(instance,chain->surface,nullptr);
        chain->surface=VK_NULL_HANDLE;
        if(chain->desc.renderWindow)ANativeWindow_release(chain->desc.renderWindow);
        chain->desc.renderWindow=window;
        if(window) {
            VkAndroidSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
            info.window=window;
            if(vkCreateAndroidSurfaceKHR(instance,&info,nullptr,&chain->surface)!=VK_SUCCESS)
                throw std::runtime_error("Unable to recreate Quest companion surface");
        }
    } else if(window)ANativeWindow_release(window);
    if(!chain->surface){chain->width=chain->height=0;return false;}
    return true;
}

namespace {
XrInstance instance=XR_NULL_HANDLE;
XrSystemId systemId=XR_NULL_SYSTEM_ID;
VkInstance vulkanInstance=VK_NULL_HANDLE;
std::once_flag initialized;
bool performanceSettings=false;
bool displayRefresh=false;
bool timespecTime=false;
void check(XrResult r,const char* operation){if(XR_FAILED(r))throw std::runtime_error(std::string(operation)+": "+std::to_string(r));}
template<class T> T function(const char* name){T value=nullptr;check(xrGetInstanceProcAddr(instance,name,reinterpret_cast<PFN_xrVoidFunction*>(&value)),name);return value;}
void initialize() {
    auto* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    JavaVM* vm=nullptr;env->GetJavaVM(&vm);
    auto local=static_cast<jobject>(SDL_AndroidGetActivity());
    // The loader retains access to the Activity throughout the native session.
    static jobject activity=env->NewGlobalRef(local);env->DeleteLocalRef(local);
    auto init=function<PFN_xrInitializeLoaderKHR>("xrInitializeLoaderKHR");
    XrLoaderInitInfoAndroidKHR loader{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};loader.applicationVM=vm;loader.applicationContext=activity;
    check(init(reinterpret_cast<XrLoaderInitInfoBaseHeaderKHR*>(&loader)),"Android OpenXR loader");
    std::vector<const char*> extensions{XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};
    uint32_t extensionCount=0;
    check(xrEnumerateInstanceExtensionProperties(nullptr,0,&extensionCount,nullptr),"extension count");
    std::vector<XrExtensionProperties> available(extensionCount,{XR_TYPE_EXTENSION_PROPERTIES});
    check(xrEnumerateInstanceExtensionProperties(nullptr,extensionCount,&extensionCount,available.data()),"extensions");
    for(const auto& extension:available) {
        if(std::strcmp(extension.extensionName,XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME)==0) {
            extensions.push_back(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME);performanceSettings=true;
        }
        if(std::strcmp(extension.extensionName,XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME)==0) {
            extensions.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);displayRefresh=true;
        }
        if(std::strcmp(extension.extensionName,XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME)==0) {
            extensions.push_back(XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME);timespecTime=true;
        }
    }
    XrInstanceCreateInfoAndroidKHR android{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};android.applicationVM=vm;android.applicationActivity=activity;
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};ci.next=&android;
    std::strcpy(ci.applicationInfo.applicationName,"Snap64 VR");ci.applicationInfo.apiVersion=XR_API_VERSION_1_0;
    ci.enabledExtensionCount=uint32_t(extensions.size());ci.enabledExtensionNames=extensions.data();
    check(xrCreateInstance(&ci,&instance),"xrCreateInstance");
    XrSystemGetInfo si{XR_TYPE_SYSTEM_GET_INFO};si.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    check(xrGetSystem(instance,&si,&systemId),"xrGetSystem");
    XrGraphicsRequirementsVulkan2KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
    check(function<PFN_xrGetVulkanGraphicsRequirements2KHR>("xrGetVulkanGraphicsRequirements2KHR")(instance,systemId,&requirements),"Vulkan requirements");
}
}
XrInstance snap_quest_xr_instance(){std::call_once(initialized,initialize);return instance;}
XrSystemId snap_quest_xr_system(){snap_quest_xr_instance();return systemId;}
double snap_quest_monotonic_time(XrTime time) {
    if(!timespecTime)throw std::runtime_error("Predicted-time animation requires XR_KHR_convert_timespec_time");
    timespec converted{};
    check(function<PFN_xrConvertTimeToTimespecTimeKHR>("xrConvertTimeToTimespecTimeKHR")(instance,time,&converted),"convert predicted display time");
    return double(converted.tv_sec)+double(converted.tv_nsec)*1e-9;
}
float snap_quest_display_refresh(XrSession session,bool request) {
    if(!displayRefresh)return 0;
    auto get=function<PFN_xrGetDisplayRefreshRateFB>("xrGetDisplayRefreshRateFB");
    if(request) {
        float wanted=80;
        if(const char* value=std::getenv("SNAP_QUEST_REFRESH"))if(std::strcmp(value,"72")==0)wanted=72;
        auto enumerate=function<PFN_xrEnumerateDisplayRefreshRatesFB>("xrEnumerateDisplayRefreshRatesFB");
        uint32_t count=0;check(enumerate(session,0,&count,nullptr),"refresh count");
        std::vector<float> rates(count);check(enumerate(session,count,&count,rates.data()),"refresh rates");
        bool supported=false;fprintf(stderr,"[SNAP-VR] supported display rates:");
        for(float rate:rates){fprintf(stderr," %.1f",rate);supported|=rate==wanted;}
        fprintf(stderr,"; requested %.1f\n",wanted);
        if(supported)check(function<PFN_xrRequestDisplayRefreshRateFB>("xrRequestDisplayRefreshRateFB")(session,wanted),"request refresh rate");
        else fprintf(stderr,"[SNAP-VR] requested display rate unsupported; benchmark cannot qualify at that rate\n");
    }
    float rate=0;check(get(session,&rate),"actual display refresh rate");return rate;
}
void snap_quest_set_performance(XrSession session) {
    if(!performanceSettings)return;
    auto set=function<PFN_xrPerfSettingsSetPerformanceLevelEXT>("xrPerfSettingsSetPerformanceLevelEXT");
    auto cpu=set(session,XR_PERF_SETTINGS_DOMAIN_CPU_EXT,XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT);
    auto gpu=set(session,XR_PERF_SETTINGS_DOMAIN_GPU_EXT,XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT);
    fprintf(stderr,"[SNAP-VR] sustained performance hints: CPU %d, GPU %d\n",int(cpu),int(gpu));
}

VkResult snap_quest_create_vulkan_instance(const VkInstanceCreateInfo* info,const VkAllocationCallbacks* allocator,VkInstance* result) {
    snap_quest_xr_instance();
    XrVulkanInstanceCreateInfoKHR ci{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};ci.systemId=systemId;
    ci.pfnGetInstanceProcAddr=vkGetInstanceProcAddr;ci.vulkanCreateInfo=info;ci.vulkanAllocator=allocator;
    VkResult status=VK_ERROR_INITIALIZATION_FAILED;
    check(function<PFN_xrCreateVulkanInstanceKHR>("xrCreateVulkanInstanceKHR")(instance,&ci,result,&status),"xrCreateVulkanInstanceKHR");
    if(status==VK_SUCCESS)vulkanInstance=*result;
    return status;
}
VkResult snap_quest_create_vulkan_device(VkPhysicalDevice physical,const VkDeviceCreateInfo* info,const VkAllocationCallbacks* allocator,VkDevice* result) {
    XrVulkanGraphicsDeviceGetInfoKHR gi{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    gi.systemId=systemId;gi.vulkanInstance=vulkanInstance;
    VkPhysicalDevice required=VK_NULL_HANDLE;
    check(function<PFN_xrGetVulkanGraphicsDevice2KHR>("xrGetVulkanGraphicsDevice2KHR")(instance,&gi,&required),"xrGetVulkanGraphicsDevice2KHR");
    if(required!=physical)throw std::runtime_error("RT64 selected a GPU incompatible with OpenXR");
    XrVulkanDeviceCreateInfoKHR ci{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};ci.systemId=systemId;
    ci.pfnGetInstanceProcAddr=vkGetInstanceProcAddr;ci.vulkanPhysicalDevice=physical;ci.vulkanCreateInfo=info;ci.vulkanAllocator=allocator;
    VkResult status=VK_ERROR_INITIALIZATION_FAILED;
    check(function<PFN_xrCreateVulkanDeviceKHR>("xrCreateVulkanDeviceKHR")(instance,&ci,result,&status),"xrCreateVulkanDeviceKHR");
    return status;
}
