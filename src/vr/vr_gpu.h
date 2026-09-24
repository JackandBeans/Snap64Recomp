#pragma once
#ifdef __ANDROID__
#include "plume_vulkan.h"
namespace snap::vr {
using VRDevice=plume::VulkanDevice;
using VRQueue=plume::VulkanCommandQueue;
using VRTexture=plume::VulkanTexture;
}
#else
struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12Resource;
namespace snap::vr {
using VRDevice=ID3D12Device;
using VRQueue=ID3D12CommandQueue;
using VRTexture=ID3D12Resource;
}
#endif
