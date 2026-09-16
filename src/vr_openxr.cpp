/**
 * @file vr_openxr.cpp
 * @brief The OpenXR session behind vr_openxr.h.
 *
 * One session on the renderer's Direct3D 12 device, two colour swapchains
 * for the eyes and one for the flat picture, a seated reference space that
 * the port recentres itself, and one action set for the controllers. The
 * renderer talks to it through RT64::SnapVR::Interface
 * (lib/rt64/src/hle/rt64_snap_vr.h): the present thread paces on
 * xrWaitFrame, copies the eye pictures into the acquired images and ends
 * the frame with a projection layer and, when there is a flat picture to
 * show, a quad layer; the workload thread asks for the eyes' poses when it
 * draws them.
 *
 * Colour: the eye images are sRGB-typed and written through a plain UNORM
 * view, so the renderer's gamma-encoded bytes reach the compositor untouched
 * and are displayed as the desktop shows them. Casting a fully typed
 * resource to its sRGB or non-sRGB twin is what Direct3D 12 promises on
 * every driver since RS2.
 *
 * Every OpenXR failure that can happen after the session is open is logged
 * once and turns the session off; the desktop window carries on.
 */
#if defined(SNAP_HAS_OPENXR) && SNAP_HAS_OPENXR

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "plume_d3d12.h"

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D12
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "hle/rt64_snap_diag.h"
#include "hle/rt64_snap_vr.h"
#include "settings.h"
#include "vr_openxr.h"

// overlay_hook.cpp: true while a course's code overlay is resident.
namespace snap { extern std::atomic<bool> g_app_level_resident; }

namespace {

// The game's buttons, in the layout input.cpp uses.
constexpr uint16_t BtnA = 0x8000;
constexpr uint16_t BtnB = 0x4000;
constexpr uint16_t BtnZ = 0x2000;
constexpr uint16_t BtnStart = 0x1000;
constexpr uint16_t BtnR = 0x0010;
constexpr uint16_t BtnCDown = 0x0004;

constexpr float Pi = 3.14159265358979f;

// The mailbox byte the cull patch reads: one while the renderer is in world
// mode, so every Pokemon within the game's own distance draws whichever way
// the head is turned (patches/src/widescreen_cull_patch.c).
constexpr uint32_t MailboxVrWorldAddr = 0x80C000E0u;
// The pause flag, read to hold the world while the pause menu is up.
constexpr uint32_t AddrIsPaused = 0x80382D20u;

const char *xrResultName(XrInstance instance, XrResult r) {
    static thread_local char buffer[XR_MAX_RESULT_STRING_SIZE];
    if ((instance != XR_NULL_HANDLE) && XR_SUCCEEDED(xrResultToString(instance, r, buffer))) {
        return buffer;
    }
    snprintf(buffer, sizeof(buffer), "XrResult %d", int(r));
    return buffer;
}

XrPosef identityPose() {
    XrPosef p;
    p.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
    p.position = { 0.0f, 0.0f, 0.0f };
    return p;
}

void rotateByQuat(const XrQuaternionf &q, const float v[3], float out[3]) {
    const float qv[4] = { q.x, q.y, q.z, q.w };
    RT64::SnapVR::quatRotate(qv, v, out);
}

// Yaw (positive left) and pitch (positive up) of a pose's forward.
void yawPitchOf(const XrQuaternionf &q, float &yaw, float &pitch) {
    static const float fwd[3] = { 0.0f, 0.0f, -1.0f };
    float f[3];
    rotateByQuat(q, fwd, f);
    yaw = std::atan2(-f[0], -f[2]);
    pitch = std::asin(std::max(-1.0f, std::min(1.0f, f[1])));
}

XrQuaternionf yawQuat(float yaw) {
    XrQuaternionf q;
    q.x = 0.0f;
    q.y = std::sin(yaw * 0.5f);
    q.z = 0.0f;
    q.w = std::cos(yaw * 0.5f);
    return q;
}

struct Chain {
    XrSwapchain handle = XR_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<XrSwapchainImageD3D12KHR> images;
    std::vector<std::unique_ptr<plume::D3D12Texture>> textures;
    int32_t acquired = -1;
    bool releasedThisFrame = false;
    bool waitPending = false;
};

struct Session final : RT64::SnapVR::Interface {
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace localSpace = XR_NULL_HANDLE;
    XrSpace viewSpace = XR_NULL_HANDLE;
    XrSpace appSpace = XR_NULL_HANDLE;
    XrSessionState state = XR_SESSION_STATE_UNKNOWN;
    std::atomic<bool> alive{false};
    std::atomic<bool> running{false};
    bool focused = false;
    bool frameBegun = false;
    bool shouldRender = false;
    bool recentred = false;
    XrTime frameDisplayTime = 0;
    PFN_xrGetDisplayRefreshRateFB getRefreshRateFB = nullptr;

    plume::D3D12Device *device = nullptr;
    ID3D12CommandQueue *queue = nullptr;
    int64_t swapchainFormat = 0;
    plume::RenderFormat viewFormat = plume::RenderFormat::R8G8B8A8_UNORM;
    bool srgbFormat = false;
    uint32_t recommendedWidth = 0;
    uint32_t recommendedHeight = 0;
    uint32_t maxImageWidth = 0;
    uint32_t maxImageHeight = 0;
    Chain eyes[2];
    Chain screen;

    std::mutex timingMutex;
    RT64::SnapVR::Timing timing;

    std::mutex viewsMutex;
    RT64::SnapVR::Views lastViews;

    std::atomic<bool> headValid{false};
    std::atomic<float> headYaw{0.0f};
    std::atomic<float> headPitch{0.0f};

    XrActionSet actionSet = XR_NULL_HANDLE;
    XrAction actShoot = XR_NULL_HANDLE;
    XrAction actZoom = XR_NULL_HANDLE;
    XrAction actThrow = XR_NULL_HANDLE;
    XrAction actFlute = XR_NULL_HANDLE;
    XrAction actDash = XR_NULL_HANDLE;
    XrAction actStart = XR_NULL_HANDLE;
    XrAction actRecenter = XR_NULL_HANDLE;
    XrAction actStick = XR_NULL_HANDLE;
    std::atomic<uint16_t> buttons{0};
    std::atomic<float> stickX{0.0f};
    std::atomic<float> stickY{0.0f};
    int64_t recenterHeldSince = 0;
    bool recenterFired = false;

    std::mutex camMutex;
    // The last cameras the game drew with, newest last, each stamped with the
    // game frame it belongs to: the renderer asks for the one belonging to the
    // frame it is rendering rather than taking whatever the game thread, which
    // runs ahead of it, published most recently.
    static constexpr uint32_t CamRing = 16;
    RT64::SnapVR::GameCamera camRing[CamRing];
    uint32_t camWrite = 0;
    std::atomic<bool> pausedInCourse{false};
    std::atomic<uint32_t> chainGen{0};

    bool trace = false;
    std::chrono::steady_clock::time_point lastTrace;
    uint32_t framesEnded = 0;
    uint32_t framesWithEyes = 0;
    uint32_t framesWithScreen = 0;
    uint32_t framesNoLayers = 0;
    uint32_t endFailCount = 0;
    uint32_t timeoutCount = 0;
    uint32_t acquireFailCount = 0;
    bool loggedFrameError = false;
    bool loggedAcquireError = false;
    bool loggedLocateError = false;

    // ---------------------------------------------------------------
    // Set-up
    // ---------------------------------------------------------------

    bool check(XrResult r, const char *what) {
        if (XR_SUCCEEDED(r)) {
            return true;
        }
        printf("[SNAP-VR] %s failed: %s\n", what, xrResultName(instance, r));
        fflush(stdout);
        return false;
    }

    bool hasExtension(const std::vector<XrExtensionProperties> &exts, const char *name) {
        for (const auto &e : exts) {
            if (strcmp(e.extensionName, name) == 0) {
                return true;
            }
        }
        return false;
    }

    bool create(plume::D3D12Device *d3dDevice, ID3D12CommandQueue *d3dQueue) {
        device = d3dDevice;
        queue = d3dQueue;
        trace = (getenv("SNAP_VR_TRACE") != nullptr);

        uint32_t extCount = 0;
        XrResult r = xrEnumerateInstanceExtensionProperties(nullptr, 0, &extCount, nullptr);
        if (XR_FAILED(r)) {
            printf("[SNAP-VR] no OpenXR runtime: %s\n", xrResultName(XR_NULL_HANDLE, r));
            fflush(stdout);
            return false;
        }
        std::vector<XrExtensionProperties> exts(extCount, { XR_TYPE_EXTENSION_PROPERTIES });
        if (extCount > 0) {
            xrEnumerateInstanceExtensionProperties(nullptr, extCount, &extCount, exts.data());
        }
        if (!hasExtension(exts, XR_KHR_D3D12_ENABLE_EXTENSION_NAME)) {
            printf("[SNAP-VR] the OpenXR runtime has no Direct3D 12 support; running flat\n");
            fflush(stdout);
            return false;
        }
        std::vector<const char *> enabled = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
        const bool refreshExt = hasExtension(exts, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
        if (refreshExt) {
            enabled.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
        }

        XrInstanceCreateInfo ici{ XR_TYPE_INSTANCE_CREATE_INFO };
        strncpy(ici.applicationInfo.applicationName, "Snap64 Recomp", XR_MAX_APPLICATION_NAME_SIZE - 1);
        ici.applicationInfo.applicationVersion = 1;
        strncpy(ici.applicationInfo.engineName, "Snap64 Recomp", XR_MAX_ENGINE_NAME_SIZE - 1);
        ici.applicationInfo.engineVersion = 1;
        ici.applicationInfo.apiVersion = XR_API_VERSION_1_0;
        ici.enabledExtensionCount = uint32_t(enabled.size());
        ici.enabledExtensionNames = enabled.data();
        r = xrCreateInstance(&ici, &instance);
        if (XR_FAILED(r)) {
            printf("[SNAP-VR] xrCreateInstance failed: %s (is an OpenXR runtime set as active?)\n", xrResultName(XR_NULL_HANDLE, r));
            fflush(stdout);
            instance = XR_NULL_HANDLE;
            return false;
        }

        XrInstanceProperties ip{ XR_TYPE_INSTANCE_PROPERTIES };
        if (XR_SUCCEEDED(xrGetInstanceProperties(instance, &ip))) {
            printf("[SNAP-VR] runtime: %s %u.%u.%u\n", ip.runtimeName,
                unsigned(XR_VERSION_MAJOR(ip.runtimeVersion)), unsigned(XR_VERSION_MINOR(ip.runtimeVersion)), unsigned(XR_VERSION_PATCH(ip.runtimeVersion)));
        }

        XrSystemGetInfo sgi{ XR_TYPE_SYSTEM_GET_INFO };
        sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        r = xrGetSystem(instance, &sgi, &systemId);
        if (XR_FAILED(r)) {
            if (r == XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
                printf("[SNAP-VR] no headset is connected (the runtime reports no head-mounted display); running flat\n");
            }
            else {
                printf("[SNAP-VR] xrGetSystem failed: %s; running flat\n", xrResultName(instance, r));
            }
            fflush(stdout);
            return false;
        }

        XrSystemProperties sp{ XR_TYPE_SYSTEM_PROPERTIES };
        if (XR_SUCCEEDED(xrGetSystemProperties(instance, systemId, &sp))) {
            maxImageWidth = sp.graphicsProperties.maxSwapchainImageWidth;
            maxImageHeight = sp.graphicsProperties.maxSwapchainImageHeight;
            printf("[SNAP-VR] headset: %s (orientation %s, position %s)\n", sp.systemName,
                sp.trackingProperties.orientationTracking ? "tracked" : "untracked",
                sp.trackingProperties.positionTracking ? "tracked" : "untracked");
        }

        uint32_t viewCount = 0;
        xrEnumerateViewConfigurationViews(instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);
        if (viewCount < 2) {
            printf("[SNAP-VR] the headset offers %u views, not two; running flat\n", viewCount);
            fflush(stdout);
            return false;
        }
        std::vector<XrViewConfigurationView> vcv(viewCount, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
        if (!check(xrEnumerateViewConfigurationViews(instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount, vcv.data()), "xrEnumerateViewConfigurationViews")) {
            return false;
        }
        recommendedWidth = vcv[0].recommendedImageRectWidth;
        recommendedHeight = vcv[0].recommendedImageRectHeight;

        PFN_xrGetD3D12GraphicsRequirementsKHR getRequirements = nullptr;
        xrGetInstanceProcAddr(instance, "xrGetD3D12GraphicsRequirementsKHR", reinterpret_cast<PFN_xrVoidFunction *>(&getRequirements));
        if (getRequirements == nullptr) {
            printf("[SNAP-VR] xrGetD3D12GraphicsRequirementsKHR is missing; running flat\n");
            fflush(stdout);
            return false;
        }
        XrGraphicsRequirementsD3D12KHR req{ XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR };
        if (!check(getRequirements(instance, systemId, &req), "xrGetD3D12GraphicsRequirementsKHR")) {
            return false;
        }
        if (device->adapter != nullptr) {
            DXGI_ADAPTER_DESC1 desc = {};
            if (SUCCEEDED(device->adapter->GetDesc1(&desc))) {
                if ((desc.AdapterLuid.LowPart != req.adapterLuid.LowPart) || (desc.AdapterLuid.HighPart != req.adapterLuid.HighPart)) {
                    printf("[SNAP-VR] the headset is driven by a different GPU than the one the renderer opened; the runtime may refuse the session\n");
                }
            }
        }
        if (refreshExt) {
            xrGetInstanceProcAddr(instance, "xrGetDisplayRefreshRateFB", reinterpret_cast<PFN_xrVoidFunction *>(&getRefreshRateFB));
        }

        XrGraphicsBindingD3D12KHR binding{ XR_TYPE_GRAPHICS_BINDING_D3D12_KHR };
        binding.device = device->d3d;
        binding.queue = queue;
        XrSessionCreateInfo sci{ XR_TYPE_SESSION_CREATE_INFO };
        sci.next = &binding;
        sci.systemId = systemId;
        r = xrCreateSession(instance, &sci, &session);
        if (XR_FAILED(r)) {
            printf("[SNAP-VR] xrCreateSession failed: %s; running flat\n", xrResultName(instance, r));
            fflush(stdout);
            session = XR_NULL_HANDLE;
            return false;
        }

        XrReferenceSpaceCreateInfo rci{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
        rci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        rci.poseInReferenceSpace = identityPose();
        if (!check(xrCreateReferenceSpace(session, &rci, &localSpace), "xrCreateReferenceSpace(LOCAL)")) {
            return false;
        }
        rci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
        if (!check(xrCreateReferenceSpace(session, &rci, &viewSpace), "xrCreateReferenceSpace(VIEW)")) {
            return false;
        }
        rci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        if (!check(xrCreateReferenceSpace(session, &rci, &appSpace), "xrCreateReferenceSpace(app)")) {
            return false;
        }

        if (!chooseFormat()) {
            return false;
        }

        const float renderScale = std::max(0.5f, std::min(2.0f, snap::settings().vr_render_scale));
        uint32_t eyeW = uint32_t(std::lround(double(recommendedWidth) * renderScale));
        uint32_t eyeH = uint32_t(std::lround(double(recommendedHeight) * renderScale));
        if (maxImageWidth > 0) eyeW = std::min(eyeW, maxImageWidth);
        if (maxImageHeight > 0) eyeH = std::min(eyeH, maxImageHeight);
        eyeW = std::max(eyeW, 256u);
        eyeH = std::max(eyeH, 256u);
        for (uint32_t e = 0; e < 2; e++) {
            if (!createChain(eyes[e], eyeW, eyeH, e == 0 ? "left eye" : "right eye")) {
                return false;
            }
        }
        printf("[SNAP-VR] eyes: %ux%u each (the runtime recommends %ux%u, scale %.2f), format %s%s\n",
            eyeW, eyeH, recommendedWidth, recommendedHeight, renderScale,
            (swapchainFormat == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) ? "R8G8B8A8_UNORM_SRGB" :
            (swapchainFormat == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) ? "B8G8R8A8_UNORM_SRGB" :
            (swapchainFormat == DXGI_FORMAT_R8G8B8A8_UNORM) ? "R8G8B8A8_UNORM" : "B8G8R8A8_UNORM",
            srgbFormat ? "" : " (no sRGB format offered: colours may look washed out)");

        if (!createActions()) {
            return false;
        }

        alive.store(true, std::memory_order_release);
        printf("[SNAP-VR] session created; put the headset on\n");
        fflush(stdout);
        return true;
    }

    bool chooseFormat() {
        uint32_t count = 0;
        if (!check(xrEnumerateSwapchainFormats(session, 0, &count, nullptr), "xrEnumerateSwapchainFormats")) {
            return false;
        }
        std::vector<int64_t> formats(count);
        if (count > 0) {
            xrEnumerateSwapchainFormats(session, count, &count, formats.data());
        }
        auto has = [&](int64_t f) {
            return std::find(formats.begin(), formats.end(), f) != formats.end();
        };
        if (has(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)) {
            swapchainFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            viewFormat = plume::RenderFormat::R8G8B8A8_UNORM;
            srgbFormat = true;
        }
        else if (has(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)) {
            swapchainFormat = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            viewFormat = plume::RenderFormat::B8G8R8A8_UNORM;
            srgbFormat = true;
        }
        else if (has(DXGI_FORMAT_R8G8B8A8_UNORM)) {
            swapchainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
            viewFormat = plume::RenderFormat::R8G8B8A8_UNORM;
        }
        else if (has(DXGI_FORMAT_B8G8R8A8_UNORM)) {
            swapchainFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
            viewFormat = plume::RenderFormat::B8G8R8A8_UNORM;
        }
        else {
            printf("[SNAP-VR] the runtime offers no 8-bit colour swapchain format; running flat\n");
            fflush(stdout);
            return false;
        }
        return true;
    }

    bool createChain(Chain &chain, uint32_t width, uint32_t height, const char *name) {
        destroyChain(chain);
        XrSwapchainCreateInfo sci{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
        sci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        sci.format = swapchainFormat;
        sci.sampleCount = 1;
        sci.width = width;
        sci.height = height;
        sci.faceCount = 1;
        sci.arraySize = 1;
        sci.mipCount = 1;
        XrResult r = xrCreateSwapchain(session, &sci, &chain.handle);
        if (XR_FAILED(r)) {
            printf("[SNAP-VR] xrCreateSwapchain (%s, %ux%u) failed: %s\n", name, width, height, xrResultName(instance, r));
            fflush(stdout);
            chain.handle = XR_NULL_HANDLE;
            return false;
        }
        chain.width = width;
        chain.height = height;
        uint32_t imageCount = 0;
        xrEnumerateSwapchainImages(chain.handle, 0, &imageCount, nullptr);
        chain.images.assign(imageCount, { XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR });
        if (!check(xrEnumerateSwapchainImages(chain.handle, imageCount, &imageCount, reinterpret_cast<XrSwapchainImageBaseHeader *>(chain.images.data())), "xrEnumerateSwapchainImages")) {
            destroyChain(chain);
            return false;
        }
        chain.textures.clear();
        for (uint32_t i = 0; i < imageCount; i++) {
            // The runtime's image, wrapped the way the swap chain's own
            // buffers are (plume_d3d12.cpp, D3D12SwapChain::setTextures):
            // no allocation of ours, so the wrapper never releases it. The
            // image is handed over in the render-target state and must be
            // returned in it, which a colour write neither leaves nor needs
            // to enter.
            auto tex = std::make_unique<plume::D3D12Texture>();
            tex->d3d = chain.images[i].texture;
            tex->device = device;
            tex->desc = plume::RenderTextureDesc::ColorTarget(width, height, viewFormat);
            tex->resourceStates = D3D12_RESOURCE_STATE_RENDER_TARGET;
            tex->layout = plume::RenderTextureLayout::COLOR_WRITE;
            chain.textures.emplace_back(std::move(tex));
        }
        chain.acquired = -1;
        chain.releasedThisFrame = false;
        chainGen.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void destroyChain(Chain &chain) {
        chain.textures.clear();
        chain.images.clear();
        if (chain.handle != XR_NULL_HANDLE) {
            xrDestroySwapchain(chain.handle);
            chain.handle = XR_NULL_HANDLE;
        }
        chain.width = chain.height = 0;
        chain.acquired = -1;
        chain.releasedThisFrame = false;
        chainGen.fetch_add(1, std::memory_order_relaxed);
    }

    bool makeAction(XrActionType type, const char *name, const char *localized, XrAction &out) {
        XrActionCreateInfo aci{ XR_TYPE_ACTION_CREATE_INFO };
        aci.actionType = type;
        strncpy(aci.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
        strncpy(aci.localizedActionName, localized, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
        aci.countSubactionPaths = 0;
        aci.subactionPaths = nullptr;
        return check(xrCreateAction(actionSet, &aci, &out), name);
    }

    XrPath path(const char *s) {
        XrPath p = XR_NULL_PATH;
        xrStringToPath(instance, s, &p);
        return p;
    }

    void suggest(const char *profile, const std::vector<std::pair<XrAction, const char *>> &pairs) {
        std::vector<XrActionSuggestedBinding> bindings;
        for (const auto &pr : pairs) {
            bindings.push_back({ pr.first, path(pr.second) });
        }
        XrInteractionProfileSuggestedBinding sb{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
        sb.interactionProfile = path(profile);
        sb.countSuggestedBindings = uint32_t(bindings.size());
        sb.suggestedBindings = bindings.data();
        const XrResult r = xrSuggestInteractionProfileBindings(instance, &sb);
        if (XR_FAILED(r)) {
            printf("[SNAP-VR] bindings for %s refused: %s\n", profile, xrResultName(instance, r));
        }
    }

    bool createActions() {
        XrActionSetCreateInfo asci{ XR_TYPE_ACTION_SET_CREATE_INFO };
        strncpy(asci.actionSetName, "gameplay", XR_MAX_ACTION_SET_NAME_SIZE - 1);
        strncpy(asci.localizedActionSetName, "Gameplay", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
        asci.priority = 0;
        if (!check(xrCreateActionSet(instance, &asci, &actionSet), "xrCreateActionSet")) {
            return false;
        }
        bool ok = true;
        ok = ok && makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "shoot", "Take the photo (A)", actShoot);
        ok = ok && makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "zoom", "Zoom (Z)", actZoom);
        ok = ok && makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "throw", "Pester Ball (B)", actThrow);
        ok = ok && makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "flute", "Poke Flute (C-Down)", actFlute);
        ok = ok && makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "dash", "Dash Engine (R)", actDash);
        ok = ok && makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "start", "Start", actStart);
        ok = ok && makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "recenter", "Recentre the view (hold)", actRecenter);
        ok = ok && makeAction(XR_ACTION_TYPE_VECTOR2F_INPUT, "stick", "Control Stick", actStick);
        if (!ok) {
            return false;
        }

        // The Quest's Touch controllers (Touch Plus included). The right
        // hand holds the camera: trigger shoots, grip zooms, A throws, B is
        // the flute. The left hand: grip is the dash engine, Y and the menu
        // button are Start, the stick is the Control Stick, its click held
        // recentres.
        suggest("/interaction_profiles/oculus/touch_controller", {
            { actShoot, "/user/hand/right/input/trigger/value" },
            { actZoom, "/user/hand/right/input/squeeze/value" },
            { actThrow, "/user/hand/right/input/a/click" },
            { actFlute, "/user/hand/right/input/b/click" },
            { actDash, "/user/hand/left/input/squeeze/value" },
            { actStart, "/user/hand/left/input/y/click" },
            { actStart, "/user/hand/left/input/menu/click" },
            { actRecenter, "/user/hand/left/input/thumbstick/click" },
            { actStick, "/user/hand/left/input/thumbstick" },
            { actStick, "/user/hand/right/input/thumbstick" },
        });
        suggest("/interaction_profiles/valve/index_controller", {
            { actShoot, "/user/hand/right/input/trigger/value" },
            { actZoom, "/user/hand/right/input/squeeze/value" },
            { actThrow, "/user/hand/right/input/a/click" },
            { actFlute, "/user/hand/right/input/b/click" },
            { actDash, "/user/hand/left/input/squeeze/value" },
            { actStart, "/user/hand/left/input/b/click" },
            { actRecenter, "/user/hand/left/input/thumbstick/click" },
            { actStick, "/user/hand/left/input/thumbstick" },
            { actStick, "/user/hand/right/input/thumbstick" },
        });
        suggest("/interaction_profiles/htc/vive_controller", {
            { actShoot, "/user/hand/right/input/trigger/value" },
            { actZoom, "/user/hand/right/input/squeeze/click" },
            { actThrow, "/user/hand/right/input/trackpad/click" },
            { actFlute, "/user/hand/left/input/trackpad/click" },
            { actDash, "/user/hand/left/input/squeeze/click" },
            { actStart, "/user/hand/left/input/menu/click" },
            { actRecenter, "/user/hand/right/input/menu/click" },
            { actStick, "/user/hand/left/input/trackpad" },
        });
        suggest("/interaction_profiles/khr/simple_controller", {
            { actShoot, "/user/hand/right/input/select/click" },
            { actStart, "/user/hand/left/input/menu/click" },
            { actRecenter, "/user/hand/right/input/menu/click" },
        });

        XrSessionActionSetsAttachInfo attach{ XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
        attach.countActionSets = 1;
        attach.actionSets = &actionSet;
        return check(xrAttachSessionActionSets(session, &attach), "xrAttachSessionActionSets");
    }

    void destroy() {
        alive.store(false, std::memory_order_release);
        running.store(false, std::memory_order_release);
        if (session != XR_NULL_HANDLE) {
            if (state == XR_SESSION_STATE_SYNCHRONIZED || state == XR_SESSION_STATE_VISIBLE || state == XR_SESSION_STATE_FOCUSED || state == XR_SESSION_STATE_READY) {
                xrRequestExitSession(session);
            }
        }
        for (uint32_t e = 0; e < 2; e++) {
            destroyChain(eyes[e]);
        }
        destroyChain(screen);
        if (actionSet != XR_NULL_HANDLE) {
            xrDestroyActionSet(actionSet);
            actionSet = XR_NULL_HANDLE;
        }
        if (appSpace != XR_NULL_HANDLE) { xrDestroySpace(appSpace); appSpace = XR_NULL_HANDLE; }
        if (viewSpace != XR_NULL_HANDLE) { xrDestroySpace(viewSpace); viewSpace = XR_NULL_HANDLE; }
        if (localSpace != XR_NULL_HANDLE) { xrDestroySpace(localSpace); localSpace = XR_NULL_HANDLE; }
        if (session != XR_NULL_HANDLE) {
            xrDestroySession(session);
            session = XR_NULL_HANDLE;
        }
        if (instance != XR_NULL_HANDLE) {
            xrDestroyInstance(instance);
            instance = XR_NULL_HANDLE;
        }
    }

    void die(const char *why) {
        if (alive.load(std::memory_order_acquire)) {
            printf("[SNAP-VR] %s; the headset is off for the rest of this run\n", why);
            fflush(stdout);
        }
        alive.store(false, std::memory_order_release);
        running.store(false, std::memory_order_release);
        headValid.store(false, std::memory_order_relaxed);
        buttons.store(0, std::memory_order_relaxed);
    }

    // ---------------------------------------------------------------
    // The session's life, on the present thread
    // ---------------------------------------------------------------

    void pollEvents() {
        XrEventDataBuffer ev{ XR_TYPE_EVENT_DATA_BUFFER };
        while (xrPollEvent(instance, &ev) == XR_SUCCESS) {
            switch (ev.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                const auto *sc = reinterpret_cast<const XrEventDataSessionStateChanged *>(&ev);
                state = sc->state;
                switch (state) {
                case XR_SESSION_STATE_READY: {
                    XrSessionBeginInfo bi{ XR_TYPE_SESSION_BEGIN_INFO };
                    bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    const XrResult r = xrBeginSession(session, &bi);
                    if (XR_SUCCEEDED(r)) {
                        running.store(true, std::memory_order_release);
                        printf("[SNAP-VR] session running\n");
                    }
                    else {
                        printf("[SNAP-VR] xrBeginSession failed: %s\n", xrResultName(instance, r));
                    }
                    fflush(stdout);
                    break;
                }
                case XR_SESSION_STATE_FOCUSED:
                    focused = true;
                    if (!recentred) {
                        recentre("first focus");
                    }
                    break;
                case XR_SESSION_STATE_VISIBLE:
                case XR_SESSION_STATE_SYNCHRONIZED:
                    focused = false;
                    break;
                case XR_SESSION_STATE_STOPPING:
                    focused = false;
                    running.store(false, std::memory_order_release);
                    headValid.store(false, std::memory_order_relaxed);
                    buttons.store(0, std::memory_order_relaxed);
                    xrEndSession(session);
                    printf("[SNAP-VR] session stopped (headset idle or removed); the window carries on\n");
                    fflush(stdout);
                    break;
                case XR_SESSION_STATE_EXITING:
                case XR_SESSION_STATE_LOSS_PENDING:
                    die("the runtime ended the session");
                    break;
                default:
                    break;
                }
                break;
            }
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                die("the OpenXR runtime is going away");
                break;
            case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
                recentred = false;
                break;
            case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
                if (trace) {
                    printf("[SNAP-VR] controllers changed profile\n");
                }
                break;
            default:
                break;
            }
            ev = XrEventDataBuffer{ XR_TYPE_EVENT_DATA_BUFFER };
        }
    }

    // Puts the seat's origin where the head is now, facing the way it
    // faces, yaw only: the world's forward is the cart's forward from here.
    void recentre(const char *why) {
        if (session == XR_NULL_HANDLE) {
            return;
        }
        XrTime t = frameDisplayTime;
        if (t == 0) {
            return;
        }
        XrSpaceLocation loc{ XR_TYPE_SPACE_LOCATION };
        if (XR_FAILED(xrLocateSpace(viewSpace, localSpace, t, &loc))) {
            return;
        }
        const bool ok = (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) && (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT);
        if (!ok) {
            return;
        }
        float yaw, pitch;
        yawPitchOf(loc.pose.orientation, yaw, pitch);
        XrReferenceSpaceCreateInfo rci{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
        rci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        rci.poseInReferenceSpace.position = loc.pose.position;
        rci.poseInReferenceSpace.orientation = yawQuat(yaw);
        XrSpace fresh = XR_NULL_HANDLE;
        if (XR_FAILED(xrCreateReferenceSpace(session, &rci, &fresh))) {
            return;
        }
        // The workload thread locates views in the app space; swap under
        // the views lock so it never sees a destroyed handle.
        XrSpace old;
        {
            std::scoped_lock<std::mutex> lock(viewsMutex);
            old = appSpace;
            appSpace = fresh;
        }
        if (old != XR_NULL_HANDLE) {
            xrDestroySpace(old);
        }
        recentred = true;
        printf("[SNAP-VR] view recentred (%s)\n", why);
        fflush(stdout);
    }

    void syncActions() {
        if (!running.load(std::memory_order_acquire)) {
            return;
        }
        XrActiveActionSet active{ actionSet, XR_NULL_PATH };
        XrActionsSyncInfo si{ XR_TYPE_ACTIONS_SYNC_INFO };
        si.countActiveActionSets = 1;
        si.activeActionSets = &active;
        const XrResult r = xrSyncActions(session, &si);
        if (r == XR_SESSION_NOT_FOCUSED || XR_FAILED(r)) {
            buttons.store(0, std::memory_order_relaxed);
            stickX.store(0.0f, std::memory_order_relaxed);
            stickY.store(0.0f, std::memory_order_relaxed);
            return;
        }
        auto down = [&](XrAction a) {
            XrActionStateGetInfo gi{ XR_TYPE_ACTION_STATE_GET_INFO };
            gi.action = a;
            gi.subactionPath = XR_NULL_PATH;
            XrActionStateBoolean st{ XR_TYPE_ACTION_STATE_BOOLEAN };
            if (XR_FAILED(xrGetActionStateBoolean(session, &gi, &st))) {
                return false;
            }
            return st.isActive && st.currentState;
        };
        uint16_t b = 0;
        if (down(actShoot)) b |= BtnA;
        if (down(actThrow)) b |= BtnB;
        if (down(actZoom)) b |= BtnZ;
        if (down(actStart)) b |= BtnStart;
        if (down(actDash)) b |= BtnR;
        if (down(actFlute)) b |= BtnCDown;
        buttons.store(b, std::memory_order_relaxed);

        XrActionStateGetInfo gi{ XR_TYPE_ACTION_STATE_GET_INFO };
        gi.action = actStick;
        gi.subactionPath = XR_NULL_PATH;
        XrActionStateVector2f sv{ XR_TYPE_ACTION_STATE_VECTOR2F };
        float sx = 0.0f, sy = 0.0f;
        if (XR_SUCCEEDED(xrGetActionStateVector2f(session, &gi, &sv)) && sv.isActive) {
            sx = sv.currentState.x;
            sy = sv.currentState.y;
            const float mag = std::sqrt(sx * sx + sy * sy);
            const float dead = 0.2f;
            if (mag < dead) {
                sx = sy = 0.0f;
            }
            else {
                const float scaled = std::min(1.0f, (mag - dead) / (1.0f - dead));
                sx = sx / mag * scaled;
                sy = sy / mag * scaled;
            }
        }
        stickX.store(sx, std::memory_order_relaxed);
        stickY.store(sy, std::memory_order_relaxed);

        // Recentre: the button held for a second, once per hold.
        const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (down(actRecenter)) {
            if (recenterHeldSince == 0) {
                recenterHeldSince = now;
            }
            else if (!recenterFired && (now - recenterHeldSince >= 1000)) {
                recentre("button held");
                recenterFired = true;
            }
        }
        else {
            recenterHeldSince = 0;
            recenterFired = false;
        }
    }

    void updateHead(XrTime t) {
        XrSpace space;
        {
            std::scoped_lock<std::mutex> lock(viewsMutex);
            space = appSpace;
        }
        XrSpaceLocation loc{ XR_TYPE_SPACE_LOCATION };
        if (XR_FAILED(xrLocateSpace(viewSpace, space, t, &loc)) || !(loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
            headValid.store(false, std::memory_order_relaxed);
            return;
        }
        float yaw, pitch;
        yawPitchOf(loc.pose.orientation, yaw, pitch);
        headYaw.store(yaw, std::memory_order_relaxed);
        headPitch.store(pitch, std::memory_order_relaxed);
        headValid.store(true, std::memory_order_release);
    }

    // ---------------------------------------------------------------
    // RT64::SnapVR::Interface
    // ---------------------------------------------------------------

    bool sessionAlive() override {
        return alive.load(std::memory_order_acquire);
    }

    bool active() override {
        return alive.load(std::memory_order_acquire) && running.load(std::memory_order_acquire);
    }

    bool worldRunning() override {
        return snap::g_app_level_resident.load(std::memory_order_relaxed);
    }

    bool gameCameraForFrame(uint32_t gameFrame, float weight, RT64::SnapVR::GameCamera &out) override {
        std::scoped_lock<std::mutex> lock(camMutex);
        // The newest entry no later than the frame asked for, and the one
        // before it: the frame being rendered sits between them.
        int32_t best = -1;
        for (uint32_t k = 0; k < CamRing; k++) {
            const RT64::SnapVR::GameCamera &e = camRing[k];
            if (!e.valid) {
                continue;
            }
            if (e.gameFrame > gameFrame) {
                continue;
            }
            if ((best < 0) || (e.gameFrame > camRing[best].gameFrame)) {
                best = int32_t(k);
            }
        }
        if (best < 0) {
            out = RT64::SnapVR::GameCamera();
            out.worldMode = worldRunning();
            return false;
        }

        out = camRing[best];
        int32_t prev = -1;
        for (uint32_t k = 0; k < CamRing; k++) {
            const RT64::SnapVR::GameCamera &e = camRing[k];
            if (!e.valid || (e.gameFrame >= out.gameFrame)) {
                continue;
            }
            if ((prev < 0) || (e.gameFrame > camRing[prev].gameFrame)) {
                prev = int32_t(k);
            }
        }
        // Interpolated towards the previous frame's camera by the same weight
        // the geometry is drawn at, so the eyes glide with the world instead of
        // stepping once per game tick.
        const float w = std::max(0.0f, std::min(1.0f, weight));
        if ((prev >= 0) && camRing[prev].rideDriving && out.rideDriving && (w < 1.0f)) {
            const RT64::SnapVR::GameCamera &p = camRing[prev];
            for (uint32_t i = 0; i < 3; i++) {
                out.eye[i] = p.eye[i] + (out.eye[i] - p.eye[i]) * w;
                out.at[i] = p.at[i] + (out.at[i] - p.at[i]) * w;
                out.cartPos[i] = p.cartPos[i] + (out.cartPos[i] - p.cartPos[i]) * w;
            }
        }
        out.worldMode = worldRunning();
        out.unitsPerMetre = std::max(20.0f, std::min(400.0f, snap::settings().vr_world_scale));
        out.hudDistanceMetres = 2.0f;
        return true;
    }

    bool locateViews(int64_t displayTime, RT64::SnapVR::Views &out) override {
        if (!active()) {
            return false;
        }
        XrSpace space;
        {
            std::scoped_lock<std::mutex> lock(viewsMutex);
            space = appSpace;
        }
        XrViewLocateInfo vli{ XR_TYPE_VIEW_LOCATE_INFO };
        vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        vli.displayTime = displayTime;
        vli.space = space;
        XrViewState vs{ XR_TYPE_VIEW_STATE };
        XrView views[2] = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
        uint32_t count = 0;
        const XrResult r = xrLocateViews(session, &vli, &vs, 2, &count, views);
        if (XR_FAILED(r) || (count < 2) || !(vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
            if (XR_FAILED(r) && !loggedLocateError) {
                loggedLocateError = true;
                printf("[SNAP-VR] xrLocateViews failed: %s\n", xrResultName(instance, r));
                fflush(stdout);
            }
            std::scoped_lock<std::mutex> lock(viewsMutex);
            out = lastViews;
            return out.valid;
        }
        RT64::SnapVR::Views v;
        for (uint32_t e = 0; e < 2; e++) {
            RT64::SnapVR::EyeView &ev = v.eye[e];
            ev.pos[0] = views[e].pose.position.x;
            ev.pos[1] = views[e].pose.position.y;
            ev.pos[2] = views[e].pose.position.z;
            ev.quat[0] = views[e].pose.orientation.x;
            ev.quat[1] = views[e].pose.orientation.y;
            ev.quat[2] = views[e].pose.orientation.z;
            ev.quat[3] = views[e].pose.orientation.w;
            ev.fovLeft = views[e].fov.angleLeft;
            ev.fovRight = views[e].fov.angleRight;
            ev.fovUp = views[e].fov.angleUp;
            ev.fovDown = views[e].fov.angleDown;
        }
        v.displayTime = displayTime;
        v.valid = true;
        {
            std::scoped_lock<std::mutex> lock(viewsMutex);
            lastViews = v;
        }
        out = v;
        return true;
    }

    void latestTiming(RT64::SnapVR::Timing &out) override {
        std::scoped_lock<std::mutex> lock(timingMutex);
        out = timing;
    }

    bool frameWait(RT64::SnapVR::Timing &out) override {
        if (!alive.load(std::memory_order_acquire)) {
            return false;
        }
        pollEvents();
        if (!running.load(std::memory_order_acquire)) {
            return false;
        }
        XrFrameWaitInfo fwi{ XR_TYPE_FRAME_WAIT_INFO };
        XrFrameState fs{ XR_TYPE_FRAME_STATE };
        const XrResult r = xrWaitFrame(session, &fwi, &fs);
        if (XR_FAILED(r)) {
            if (!loggedFrameError) {
                loggedFrameError = true;
                printf("[SNAP-VR] xrWaitFrame failed: %s\n", xrResultName(instance, r));
                fflush(stdout);
            }
            if (r == XR_ERROR_SESSION_LOST || r == XR_ERROR_INSTANCE_LOST) {
                die("the session was lost");
            }
            return false;
        }
        shouldRender = (fs.shouldRender == XR_TRUE);
        frameDisplayTime = fs.predictedDisplayTime;
        if (focused && !recentred) {
            recentre("first frame in focus");
        }
        uint32_t rate = 0;
        if (getRefreshRateFB != nullptr) {
            float fb = 0.0f;
            if (XR_SUCCEEDED(getRefreshRateFB(session, &fb)) && (fb > 1.0f)) {
                rate = uint32_t(std::lround(fb));
            }
        }
        if ((rate == 0) && (fs.predictedDisplayPeriod > 0)) {
            rate = uint32_t(std::lround(1.0e9 / double(fs.predictedDisplayPeriod)));
        }
        {
            std::scoped_lock<std::mutex> lock(timingMutex);
            timing.predictedDisplayTime = fs.predictedDisplayTime;
            timing.predictedDisplayPeriod = fs.predictedDisplayPeriod;
            timing.refreshRate = rate;
            out = timing;
        }
        syncActions();
        updateHead(fs.predictedDisplayTime);
        return true;
    }

    bool frameBegin() override {
        if (!running.load(std::memory_order_acquire)) {
            return false;
        }
        XrFrameBeginInfo fbi{ XR_TYPE_FRAME_BEGIN_INFO };
        const XrResult r = xrBeginFrame(session, &fbi);
        if (XR_FAILED(r)) {
            if (!loggedFrameError) {
                loggedFrameError = true;
                printf("[SNAP-VR] xrBeginFrame failed: %s\n", xrResultName(instance, r));
                fflush(stdout);
            }
            if (r == XR_ERROR_SESSION_LOST) {
                die("the session was lost");
            }
            frameBegun = false;
            return false;
        }
        frameBegun = true;
        for (uint32_t e = 0; e < 2; e++) {
            eyes[e].releasedThisFrame = false;
        }
        screen.releasedThisFrame = false;
        return true;
    }

    plume::RenderTexture *acquireFrom(Chain &chain) {
        if (!frameBegun || (chain.handle == XR_NULL_HANDLE)) {
            return nullptr;
        }
        if (chain.acquired >= 0) {
            // An image acquired on an earlier frame whose wait timed out: wait
            // for it again rather than acquiring another.
            if (!chain.waitPending) {
                return nullptr;
            }
            XrSwapchainImageWaitInfo again{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
            again.timeout = 100000000;
            const XrResult rr = xrWaitSwapchainImage(chain.handle, &again);
            if (rr == XR_TIMEOUT_EXPIRED) {
                timeoutCount++;
                return nullptr;
            }
            if (XR_FAILED(rr)) {
                return nullptr;
            }
            chain.waitPending = false;
            return (uint32_t(chain.acquired) < chain.textures.size()) ? chain.textures[chain.acquired].get() : nullptr;
        }
        XrSwapchainImageAcquireInfo ai{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
        uint32_t index = 0;
        XrResult r = xrAcquireSwapchainImage(chain.handle, &ai, &index);
        if (XR_FAILED(r)) {
            if (!loggedAcquireError) {
                loggedAcquireError = true;
                printf("[SNAP-VR] xrAcquireSwapchainImage failed: %s\n", xrResultName(instance, r));
                fflush(stdout);
            }
            return nullptr;
        }
        XrSwapchainImageWaitInfo wi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
        wi.timeout = 100000000; // a tenth of a second
        r = xrWaitSwapchainImage(chain.handle, &wi);
        if (r == XR_TIMEOUT_EXPIRED) {
            // The image stays acquired: releasing one that was never waited for
            // loses it to the runtime for good, and a swapchain that runs out of
            // images stops accepting frames -- the world then disappears and the
            // runtime shows its own empty room, which on Link is a pale void.
            chain.acquired = int32_t(index);
            chain.waitPending = true;
            timeoutCount++;
            return nullptr;
        }
        if (XR_FAILED(r)) {
            XrSwapchainImageReleaseInfo ri{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
            xrReleaseSwapchainImage(chain.handle, &ri);
            acquireFailCount++;
            if (!loggedAcquireError) {
                loggedAcquireError = true;
                printf("[SNAP-VR] xrWaitSwapchainImage failed: %s\n", xrResultName(instance, r));
                fflush(stdout);
            }
            return nullptr;
        }
        chain.waitPending = false;
        if (index >= chain.textures.size()) {
            XrSwapchainImageReleaseInfo ri{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
            xrReleaseSwapchainImage(chain.handle, &ri);
            return nullptr;
        }
        chain.acquired = int32_t(index);
        return chain.textures[index].get();
    }

    void releaseTo(Chain &chain) {
        if (chain.acquired < 0) {
            return;
        }
        XrSwapchainImageReleaseInfo ri{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
        xrReleaseSwapchainImage(chain.handle, &ri);
        chain.acquired = -1;
        chain.releasedThisFrame = true;
    }

    plume::RenderTexture *acquireEyeImage(uint32_t eye) override {
        return (eye < 2) ? acquireFrom(eyes[eye]) : nullptr;
    }

    void releaseEyeImage(uint32_t eye) override {
        if (eye < 2) {
            releaseTo(eyes[eye]);
        }
    }

    plume::RenderTexture *acquireScreenImage(uint32_t width, uint32_t height) override {
        if (!frameBegun || (width == 0) || (height == 0)) {
            return nullptr;
        }
        uint32_t w = width, h = height;
        if (maxImageWidth > 0) w = std::min(w, maxImageWidth);
        if (maxImageHeight > 0) h = std::min(h, maxImageHeight);
        if ((screen.handle == XR_NULL_HANDLE) || (screen.width != w) || (screen.height != h)) {
            if (screen.acquired >= 0) {
                return nullptr;
            }
            if (!createChain(screen, w, h, "screen")) {
                return nullptr;
            }
            printf("[SNAP-VR] screen swapchain: %ux%u\n", w, h);
            fflush(stdout);
        }
        return acquireFrom(screen);
    }

    void releaseScreenImage() override {
        releaseTo(screen);
    }

    void frameEnd(const RT64::SnapVR::Views &views, bool showEyes, bool showScreen, bool screenHeadLocked, float screenAspect) override {
        if (!frameBegun) {
            return;
        }
        frameBegun = false;
        // Anything still acquired goes back unshown.
        for (uint32_t e = 0; e < 2; e++) {
            if (eyes[e].acquired >= 0) {
                releaseTo(eyes[e]);
                eyes[e].releasedThisFrame = false;
            }
        }
        if (screen.acquired >= 0) {
            releaseTo(screen);
            screen.releasedThisFrame = false;
        }

        XrSpace space;
        {
            std::scoped_lock<std::mutex> lock(viewsMutex);
            space = appSpace;
        }

        XrCompositionLayerProjectionView pv[2];
        XrCompositionLayerProjection proj{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
        XrCompositionLayerQuad quad{ XR_TYPE_COMPOSITION_LAYER_QUAD };
        const XrCompositionLayerBaseHeader *layers[2];
        uint32_t layerCount = 0;

        const bool eyesReady = showEyes && views.valid && eyes[0].releasedThisFrame && eyes[1].releasedThisFrame;
        if (shouldRender && eyesReady) {
            for (uint32_t e = 0; e < 2; e++) {
                pv[e] = XrCompositionLayerProjectionView{ XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
                pv[e].pose.position = { views.eye[e].pos[0], views.eye[e].pos[1], views.eye[e].pos[2] };
                pv[e].pose.orientation = { views.eye[e].quat[0], views.eye[e].quat[1], views.eye[e].quat[2], views.eye[e].quat[3] };
                pv[e].fov.angleLeft = views.eye[e].fovLeft;
                pv[e].fov.angleRight = views.eye[e].fovRight;
                pv[e].fov.angleUp = views.eye[e].fovUp;
                pv[e].fov.angleDown = views.eye[e].fovDown;
                pv[e].subImage.swapchain = eyes[e].handle;
                pv[e].subImage.imageRect.offset = { 0, 0 };
                pv[e].subImage.imageRect.extent = { int32_t(eyes[e].width), int32_t(eyes[e].height) };
                pv[e].subImage.imageArrayIndex = 0;
            }
            proj.layerFlags = 0;
            proj.space = space;
            proj.viewCount = 2;
            proj.views = pv;
            layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader *>(&proj);
            framesWithEyes++;
        }

        const bool screenReady = showScreen && screen.releasedThisFrame && (screen.handle != XR_NULL_HANDLE);
        if (shouldRender && screenReady) {
            const float aspect = (screenAspect > 0.1f) ? screenAspect : (4.0f / 3.0f);
            quad.layerFlags = 0;
            quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            quad.subImage.swapchain = screen.handle;
            quad.subImage.imageRect.offset = { 0, 0 };
            quad.subImage.imageRect.extent = { int32_t(screen.width), int32_t(screen.height) };
            quad.subImage.imageArrayIndex = 0;
            quad.pose = identityPose();
            if (screenHeadLocked) {
                // The viewfinder: a window ahead of the eyes, as tall as
                // the game's field of view at two metres, so the picture in
                // it is the size the world is behind it.
                const float distance = 2.0f;
                const float height = 2.0f * distance * std::tan(RT64::SnapVR::GameFovYDegrees * 0.5f * Pi / 180.0f);
                quad.space = viewSpace;
                quad.pose.position = { 0.0f, 0.0f, -distance };
                quad.size = { height * aspect, height };
            }
            else {
                // The screen: ahead of the seat, at eye height, three
                // metres away, as wide as the settings say.
                const float width = std::max(1.0f, std::min(8.0f, snap::settings().vr_screen_width));
                quad.space = space;
                quad.pose.position = { 0.0f, 0.0f, -3.0f };
                quad.size = { width, width / aspect };
            }
            layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader *>(&quad);
            framesWithScreen++;
        }

        if (layerCount == 0) {
            framesNoLayers++;
        }

        XrFrameEndInfo fei{ XR_TYPE_FRAME_END_INFO };
        fei.displayTime = frameDisplayTime;
        fei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        fei.layerCount = layerCount;
        fei.layers = layers;
        const XrResult r = xrEndFrame(session, &fei);
        if (XR_FAILED(r)) {
            if (!loggedFrameError) {
                loggedFrameError = true;
                printf("[SNAP-VR] xrEndFrame failed: %s (layers %u)\n", xrResultName(instance, r), layerCount);
                fflush(stdout);
            }
            if (r == XR_ERROR_SESSION_LOST) {
                die("the session was lost");
            }
            endFailCount++;
        }
        framesEnded++;

        if (trace) {
            const auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration<double>(now - lastTrace).count() >= 2.0) {
                lastTrace = now;
                RT64::SnapVR::Timing t;
                latestTiming(t);
                printf("[SNAP-VR] state %d focused %d rate %u frames %u eyes %u screen %u head yaw %.1f pitch %.1f world %d blank %d buttons %04X\n",
                    int(state), focused ? 1 : 0, t.refreshRate, framesEnded, framesWithEyes, framesWithScreen,
                    headYaw.load(std::memory_order_relaxed) * 180.0f / Pi, headPitch.load(std::memory_order_relaxed) * 180.0f / Pi,
                    snap::vr_world_mode() ? 1 : 0, int(framesNoLayers), unsigned(buttons.load(std::memory_order_relaxed)));
                fflush(stdout);
            }
        }
    }

    plume::RenderFormat imageFormat() override { return viewFormat; }
    uint32_t chainGeneration() override { return chainGen.load(std::memory_order_relaxed); }
    uint32_t eyeWidth() override { return eyes[0].width; }
    uint32_t eyeHeight() override { return eyes[0].height; }
    bool traceEnabled() override { return trace; }
};

std::unique_ptr<Session> g_session;
std::mutex g_session_mutex;

bool env_flag(const char *name, bool &value) {
    const char *v = getenv(name);
    if (v == nullptr) {
        return false;
    }
    value = !(v[0] == '0' && v[1] == '\0');
    return true;
}

} // namespace

namespace snap {

bool vr_wanted() {
    bool env = false;
    if (env_flag("SNAP_VR", env)) {
        return env;
    }
    return settings().vr;
}

bool vr_init(plume::RenderDevice *device, plume::RenderCommandQueue *queue, bool d3d12) {
    std::scoped_lock<std::mutex> lock(g_session_mutex);
    if (g_session != nullptr) {
        return true;
    }
    if (!vr_wanted()) {
        return false;
    }
    if (!d3d12 || (device == nullptr) || (queue == nullptr)) {
        printf("[SNAP-VR] the headset needs the Direct3D 12 renderer (graphics_api: 0); running flat\n");
        fflush(stdout);
        return false;
    }
    auto s = std::make_unique<Session>();
    plume::D3D12Device *d3dDevice = static_cast<plume::D3D12Device *>(device);
    plume::D3D12CommandQueue *d3dQueue = static_cast<plume::D3D12CommandQueue *>(queue);
    if (!s->create(d3dDevice, d3dQueue->d3d)) {
        s->destroy();
        return false;
    }
    g_session = std::move(s);
    RT64::SnapVR::instanceSlot().store(g_session.get(), std::memory_order_release);
    return true;
}

void vr_shutdown() {
    std::scoped_lock<std::mutex> lock(g_session_mutex);
    if (g_session == nullptr) {
        return;
    }
    RT64::SnapVR::instanceSlot().store(nullptr, std::memory_order_release);
    printf("[SNAP-VR] session closed: %u frames, %u with eyes, %u with the screen, %u with no layers; "
        "%u ends refused, %u image waits timed out, %u acquires failed\n",
        g_session->framesEnded, g_session->framesWithEyes, g_session->framesWithScreen, g_session->framesNoLayers,
        g_session->endFailCount, g_session->timeoutCount, g_session->acquireFailCount);
    fflush(stdout);
    g_session->destroy();
    g_session.reset();
}

bool vr_active() {
    Session *s = g_session.get();
    return (s != nullptr) && s->active();
}

uint16_t vr_buttons() {
    Session *s = g_session.get();
    return (s != nullptr) ? s->buttons.load(std::memory_order_relaxed) : 0;
}

void vr_stick(float &x, float &y) {
    Session *s = g_session.get();
    if (s == nullptr) {
        x = y = 0.0f;
        return;
    }
    x = s->stickX.load(std::memory_order_relaxed);
    y = s->stickY.load(std::memory_order_relaxed);
}

bool vr_head_yaw_pitch(float &yaw, float &pitch) {
    Session *s = g_session.get();
    if ((s == nullptr) || !s->active() || !s->headValid.load(std::memory_order_acquire)) {
        return false;
    }
    yaw = s->headYaw.load(std::memory_order_relaxed);
    pitch = s->headPitch.load(std::memory_order_relaxed);
    return true;
}

void vr_publish_camera(bool zoomed, bool rideDriving, uint32_t gameFrame,
                       const float eye[3], const float at[3], const float cartPos[3], const float cartRot[3]) {
    Session *s = g_session.get();
    if (s == nullptr) {
        return;
    }
    std::scoped_lock<std::mutex> lock(s->camMutex);
    RT64::SnapVR::GameCamera &e = s->camRing[s->camWrite % Session::CamRing];
    s->camWrite++;
    e = RT64::SnapVR::GameCamera();
    e.zoomed = zoomed;
    e.rideDriving = rideDriving;
    e.gameFrame = gameFrame;
    e.valid = true;
    for (int i = 0; i < 3; i++) {
        e.eye[i] = eye[i];
        e.at[i] = at[i];
        e.cartPos[i] = cartPos[i];
        e.cartRot[i] = cartRot[i];
    }
}

bool vr_world_mode() {
    Session *s = g_session.get();
    if ((s == nullptr) || !s->active()) {
        return false;
    }
    return s->worldRunning();
}

void vr_tick(uint8_t *rdram) {
    Session *s = g_session.get();
    if ((s == nullptr) || (rdram == nullptr)) {
        return;
    }
    const bool paused = rdram[(AddrIsPaused - 0x80000000u) ^ 3u] != 0;
    s->pausedInCourse.store(paused, std::memory_order_relaxed);
    const bool world = vr_world_mode();
    rdram[(MailboxVrWorldAddr - 0x80000000u) ^ 3u] = world ? 1u : 0u;
}

} // namespace snap

#else // no OpenXR SDK in this build

#include "vr_openxr.h"
#include <cstdio>

namespace snap {
bool vr_wanted() { return false; }
bool vr_init(plume::RenderDevice *, plume::RenderCommandQueue *, bool) { return false; }
void vr_shutdown() { }
bool vr_active() { return false; }
uint16_t vr_buttons() { return 0; }
void vr_stick(float &x, float &y) { x = y = 0.0f; }
bool vr_head_yaw_pitch(float &, float &) { return false; }
void vr_publish_camera(bool, const float[3], const float[3], const float[3], const float[3]) { }
bool vr_world_mode() { return false; }
void vr_tick(uint8_t *) { }
}

#endif
