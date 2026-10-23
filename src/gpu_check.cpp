// The start-up check on the Direct3D 12 adapter (issue #21).
//
// RT64's shaders are compiled for Shader Model 6.3 (lib/rt64/CMakeLists.txt,
// -T cs_6_3 and the rest), and plume's device selection asks an adapter for
// 6.0 only (plume_d3d12.cpp, D3D12Device's constructor). A driver whose
// highest model lies between the two is accepted; every pipeline it is then
// asked for fails to be created, with the result unread, and the first one
// used hands SetPipelineState a null pointer: an access violation inside
// Direct3D seconds after start, a black window that closes, and no message.
//
// The check runs before the renderer is set up. It walks the adapters as
// plume does, names each one in the log with its driver and its highest
// Shader Model, and refuses the one plume will take when that model is below
// the one the shaders need.

#include "settings.h"
#include "version.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(__ANDROID__)
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi.h>
#include <d3d12.h>
#endif

namespace snap {

static std::string s_failure_detail;

const std::string& renderer_failure_detail() {
    return s_failure_detail;
}

#if defined(_WIN32)

namespace {

// The model the renderer's shaders are built for, as Direct3D numbers it:
// 0x63 is 6.3.
constexpr int kNeededModel = 0x63;

struct Adapter {
    std::string name;
    std::string driver;
    unsigned long long memory = 0;
    int model = 0;
};

std::string utf8(const wchar_t* wide) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return std::string();
    }
    std::string text(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, text.data(), size, nullptr, nullptr);
    return text;
}

// The driver's version as Windows writes it, and for an NVIDIA driver the
// number NVIDIA gives it too: its last five digits, so 23.21.13.9125 is
// 391.25.
std::string driver_text(LARGE_INTEGER version, unsigned vendor) {
    const unsigned a = HIWORD(version.HighPart);
    const unsigned b = LOWORD(version.HighPart);
    const unsigned c = HIWORD(version.LowPart);
    const unsigned d = LOWORD(version.LowPart);
    char text[96];
    if (vendor == 0x10DE) {
        const unsigned nvidia = (c % 10) * 10000 + d;
        snprintf(text, sizeof(text), "%u.%u.%u.%u (NVIDIA %u.%02u)", a, b, c, d, nvidia / 100, nvidia % 100);
    } else {
        snprintf(text, sizeof(text), "%u.%u.%u.%u", a, b, c, d);
    }
    return text;
}

// The device's highest Shader Model, or 0 when it has none from 6.0 up. The
// runtime answers E_INVALIDARG to a model newer than it knows, so the
// question is asked downward from the newest.
int highest_model(ID3D12Device* device) {
    for (int model = 0x69; model >= 0x60; model--) {
        D3D12_FEATURE_DATA_SHADER_MODEL data = { static_cast<D3D_SHADER_MODEL>(model) };
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &data, sizeof(data)))) {
            return static_cast<int>(data.HighestShaderModel);
        }
    }
    return 0;
}

// The build of Windows, from the one call that does not answer with the
// version the executable's manifest names.
unsigned windows_build() {
    using RtlGetVersionFn = LONG (WINAPI*)(OSVERSIONINFOW*);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const RtlGetVersionFn get = (ntdll != nullptr)
        ? reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")))
        : nullptr;
    OSVERSIONINFOW info = {};
    info.dwOSVersionInfoSize = sizeof(info);
    return ((get != nullptr) && (get(&info) == 0)) ? info.dwBuildNumber : 0;
}

// SNAP_SM_TEST=6.2 has the chosen adapter answer that model, to see the
// refusal on a machine whose driver gives no cause for it.
int test_model() {
    const char* text = std::getenv("SNAP_SM_TEST");
    if ((text == nullptr) || (text[0] < '0') || (text[0] > '9') || (text[1] != '.') || (text[2] < '0') || (text[2] > '9')) {
        return 0;
    }
    return ((text[0] - '0') << 4) | (text[2] - '0');
}

} // namespace

static uint64_t s_adapter_memory = 0;    // the chosen adapter's dedicated video memory

uint64_t d3d12_adapter_memory() {
    return s_adapter_memory;
}

bool d3d12_adapter_check(std::string& why) {
    printf("[SNAP-D3D12] Windows build %u\n", windows_build());

    // With no factory or no adapter there is nothing to refuse here, and the
    // renderer's own setup reports what it finds.
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return true;
    }

    Adapter chosen;
    bool any = false;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; index++) {
        DXGI_ADAPTER_DESC1 desc = {};
        adapter->GetDesc1(&desc);
        if ((desc.Flags & (DXGI_ADAPTER_FLAG_REMOTE | DXGI_ADAPTER_FLAG_SOFTWARE)) != 0) {
            adapter->Release();
            continue;
        }

        Adapter seen;
        seen.name = utf8(desc.Description);
        seen.memory = desc.DedicatedVideoMemory;
        LARGE_INTEGER version = {};
        adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &version);
        seen.driver = driver_text(version, desc.VendorId);

        ID3D12Device* device = nullptr;
        if (SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
            seen.model = highest_model(device);
            device->Release();
        }
        adapter->Release();

        if (seen.model == 0) {
            printf("[SNAP-D3D12] adapter: %s, driver %s: no Direct3D 12 device with Shader Model 6\n",
                   seen.name.c_str(), seen.driver.c_str());
            continue;
        }
        printf("[SNAP-D3D12] adapter: %s, %llu MB, driver %s, Shader Model %d.%d\n",
               seen.name.c_str(), seen.memory >> 20, seen.driver.c_str(), seen.model >> 4, seen.model & 15);

        // plume takes the adapter with the most video memory, and the first
        // of two that tie.
        if (!any || (seen.memory > chosen.memory)) {
            chosen = seen;
            any = true;
        }
    }
    factory->Release();

    if (!any) {
        return true;
    }
    s_adapter_memory = chosen.memory;

    const int test = test_model();
    if (test != 0) {
        chosen.model = test;
        printf("[SNAP-D3D12] SNAP_SM_TEST: %s answers Shader Model %d.%d\n", chosen.name.c_str(), test >> 4, test & 15);
    }

    if (chosen.model >= kNeededModel) {
        return true;
    }

    char text[640];
    snprintf(text, sizeof(text),
             "The graphics driver is too old for " SNAP_PORT_NAME ".\n\n"
             "%s, driver %s, offers Shader Model %d.%d. The port needs %d.%d.\n\n"
             "Install the current driver from the GPU maker's site (NVIDIA, AMD or Intel). "
             "Windows 10 must be version 1809 or newer.",
             chosen.name.c_str(), chosen.driver.c_str(), chosen.model >> 4, chosen.model & 15,
             kNeededModel >> 4, kNeededModel & 15);
    s_failure_detail = text;
    why = s_failure_detail;
    printf("[SNAP-D3D12] refused: %s, driver %s, offers Shader Model %d.%d and the shaders need %d.%d\n",
           chosen.name.c_str(), chosen.driver.c_str(), chosen.model >> 4, chosen.model & 15,
           kNeededModel >> 4, kNeededModel & 15);
    return false;
}

#else

uint64_t d3d12_adapter_memory() {
    return 0;
}

bool d3d12_adapter_check(std::string&) {
    return true;
}

#endif

#if defined(__ANDROID__)

// Android's counterpart: a phone's GPU is reached through Vulkan alone, and
// plume makes a Vulkan 1.2 instance and device (plume_vulkan.cpp, its
// appInfo.apiVersion and the memory allocator it sets up for 1.2) with the
// swap chain extension. A device below that would fail inside the renderer
// with nothing said; it is named in the log, as every device is, and
// refused with a message instead.
namespace {

constexpr uint32_t kNeededVulkan = VK_MAKE_API_VERSION(0, 1, 2, 0);

struct VulkanSeen {
    std::string name;
    uint32_t api = 0;
    uint32_t driver = 0;
    uint32_t vendor = 0;
    uint32_t type = 0;
    bool swapchain = false;
};

std::string version_text(uint32_t v) {
    char text[32];
    snprintf(text, sizeof(text), "%u.%u.%u", VK_API_VERSION_MAJOR(v), VK_API_VERSION_MINOR(v), VK_API_VERSION_PATCH(v));
    return text;
}

// SNAP_VK_TEST=1.1 has the chosen device answer that Vulkan version, to see
// the refusal on a phone whose GPU gives no cause for it.
uint32_t test_version() {
    const char* text = std::getenv("SNAP_VK_TEST");
    if ((text == nullptr) || (text[0] < '0') || (text[0] > '9') || (text[1] != '.') || (text[2] < '0') || (text[2] > '9')) {
        return 0;
    }
    return VK_MAKE_API_VERSION(0, uint32_t(text[0] - '0'), uint32_t(text[2] - '0'), 0);
}

const char* type_name(uint32_t type) {
    switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual";
        case VK_PHYSICAL_DEVICE_TYPE_CPU: return "CPU";
        default: return "other";
    }
}

bool refuse(std::string& why, const std::string& text, const std::string& logged) {
    s_failure_detail = text;
    why = s_failure_detail;
    printf("[SNAP-VK] refused: %s\n", logged.c_str());
    fflush(stdout);
    return false;
}

} // namespace

bool vulkan_device_check(std::string& why) {
    void* lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    const auto gipa = (lib != nullptr) ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(lib, "vkGetInstanceProcAddr")) : nullptr;
    const auto create = (gipa != nullptr) ? reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr, "vkCreateInstance")) : nullptr;
    const std::string none = "This device has no Vulkan driver, and " SNAP_PORT_NAME " draws with Vulkan alone.";
    if (create == nullptr) {
        return refuse(why, none, "no Vulkan driver (libvulkan.so or vkCreateInstance missing)");
    }
    // The 1.0 instance any driver takes: each device's own version is what
    // is asked below.
    VkApplicationInfo app = {};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = SNAP_PORT_NAME;
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    const VkResult made = create(&info, nullptr, &instance);
    if (made != VK_SUCCESS) {
        char logged[64];
        snprintf(logged, sizeof(logged), "vkCreateInstance answered %d", int(made));
        return refuse(why, none, logged);
    }
    const auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(gipa(instance, "vkDestroyInstance"));
    const auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(gipa(instance, "vkEnumeratePhysicalDevices"));
    const auto properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(gipa(instance, "vkGetPhysicalDeviceProperties"));
    const auto extensions = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
        gipa(instance, "vkEnumerateDeviceExtensionProperties"));
    uint32_t count = 0;
    if ((enumerate != nullptr) && (properties != nullptr) && (extensions != nullptr)) {
        enumerate(instance, &count, nullptr);
    }
    std::vector<VkPhysicalDevice> devices(count);
    if (count > 0) {
        enumerate(instance, &count, devices.data());
    }
    // plume takes the device whose type scores highest (discrete, then
    // integrated, virtual, CPU, other), the first of two that tie.
    static const int score[] = {0, 3, 4, 2, 1};
    VulkanSeen chosen;
    int chosenScore = -1;
    for (VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties p = {};
        properties(device, &p);
        VulkanSeen seen;
        seen.name = p.deviceName;
        seen.api = p.apiVersion;
        seen.driver = p.driverVersion;
        seen.vendor = p.vendorID;
        seen.type = uint32_t(p.deviceType);
        uint32_t n = 0;
        extensions(device, nullptr, &n, nullptr);
        std::vector<VkExtensionProperties> list(n);
        if (n > 0) {
            extensions(device, nullptr, &n, list.data());
        }
        for (const VkExtensionProperties& e : list) {
            if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                seen.swapchain = true;
            }
        }
        printf("[SNAP-VK] device: %s, %s, Vulkan %s, driver 0x%08X, vendor 0x%04X, swap chain %s\n",
               seen.name.c_str(), type_name(seen.type), version_text(seen.api).c_str(), seen.driver, seen.vendor,
               seen.swapchain ? "yes" : "no");
        const int s = (seen.type <= 4) ? score[seen.type] : -1;
        if (s > chosenScore) {
            chosen = seen;
            chosenScore = s;
        }
    }
    if (destroy != nullptr) {
        destroy(instance, nullptr);
    }
    fflush(stdout);
    if (chosenScore < 0) {
        return refuse(why, none, "no Vulkan device");
    }
    const uint32_t test = test_version();
    if (test != 0) {
        chosen.api = test;
        printf("[SNAP-VK] SNAP_VK_TEST: %s answers Vulkan %s\n", chosen.name.c_str(), version_text(test).c_str());
    }
    if ((chosen.api >= kNeededVulkan) && chosen.swapchain) {
        return true;
    }
    char text[640];
    char logged[320];
    if (chosen.api < kNeededVulkan) {
        snprintf(text, sizeof(text),
                 "This device's graphics driver is too old for " SNAP_PORT_NAME ".\n\n"
                 "%s offers Vulkan %u.%u. The port needs Vulkan 1.2.\n\n"
                 "A system update for the device may bring a newer driver.",
                 chosen.name.c_str(), VK_API_VERSION_MAJOR(chosen.api), VK_API_VERSION_MINOR(chosen.api));
        snprintf(logged, sizeof(logged), "%s offers Vulkan %s and the renderer needs 1.2",
                 chosen.name.c_str(), version_text(chosen.api).c_str());
    } else {
        snprintf(text, sizeof(text),
                 "This device's graphics driver cannot show " SNAP_PORT_NAME "'s picture.\n\n"
                 "%s has no swap chain (VK_KHR_swapchain).",
                 chosen.name.c_str());
        snprintf(logged, sizeof(logged), "%s has no VK_KHR_swapchain", chosen.name.c_str());
    }
    return refuse(why, text, logged);
}

#else

bool vulkan_device_check(std::string&) {
    return true;
}

#endif

} // namespace snap
