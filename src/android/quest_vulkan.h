#pragma once
#include "plume_vulkan.h"
VkResult snap_quest_create_vulkan_instance(const VkInstanceCreateInfo*,const VkAllocationCallbacks*,VkInstance*);
VkResult snap_quest_create_vulkan_device(VkPhysicalDevice,const VkDeviceCreateInfo*,const VkAllocationCallbacks*,VkDevice*);
ANativeWindow* snap_quest_acquire_window();
ANativeWindow* snap_quest_wait_window();
bool snap_quest_window_changed(ANativeWindow*);
bool snap_quest_refresh_surface(plume::VulkanSwapChain*);
