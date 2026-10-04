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
#include <string>

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

bool d3d12_adapter_check(std::string&) {
    return true;
}

#endif

} // namespace snap
