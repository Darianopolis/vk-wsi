#pragma once

#include "vk-wsi.h"

#define VULKAN_INSTANCE_FUNCTIONS(DO)            \
    /* Loading */                                \
    DO(GetDeviceProcAddr)                        \
    /* Surface capabiltliies */                  \
    DO(GetPhysicalDeviceSurfaceCapabilities2KHR) \
    DO(GetPhysicalDeviceSurfacePresentModesKHR)  \

#define VULKAN_DEVICE_FUNCTIONS(DO) \
    /* Debug */                     \
    DO(SetDebugUtilsObjectNameEXT)  \
    /* Semaphores */                \
    DO(CreateSemaphore)             \
    DO(WaitSemaphores)              \
    DO(GetSemaphoreCounterValue)    \
    DO(DestroySemaphore)            \
    /* Fencces */                   \
    DO(CreateFence)                 \
    DO(ResetFences)                 \
    DO(WaitForFences)               \
    DO(DestroyFence)                \
    /* Image views */               \
    DO(CreateImageView)             \
    DO(DestroyImageView)            \
    /* Swapchains */                \
    DO(CreateSwapchainKHR)          \
    DO(GetSwapchainImagesKHR)       \
    DO(AcquireNextImageKHR)         \
    DO(DestroySwapchainKHR)         \
    /* Queue operations */          \
    DO(QueuePresentKHR)             \
    DO(QueueSubmit2)                \

#define VULKAN_DECLARE_FUNCTION(      funcName, ...) PFN_vk##funcName funcName;
#define VULKAN_LOAD_INSTANCE_FUNCTION(funcName, ...) functions->funcName = (PFN_vk##funcName)functions->GetInstanceProcAddr(instance, "vk"#funcName);
#define VULKAN_LOAD_DEVICE_FUNCTION(  funcName, ...) functions->funcName = (PFN_vk##funcName)functions->GetDeviceProcAddr(  device,   "vk"#funcName);

struct vkwsi_functions
{
    VULKAN_DECLARE_FUNCTION(GetInstanceProcAddr)
    VULKAN_INSTANCE_FUNCTIONS(VULKAN_DECLARE_FUNCTION)
    VULKAN_DEVICE_FUNCTIONS(VULKAN_DECLARE_FUNCTION)
};

inline
void vkwsi_init_functions(vkwsi_functions* functions, VkInstance instance, VkDevice device, PFN_vkGetInstanceProcAddr loadFn)
{
    functions->GetInstanceProcAddr = loadFn;

    VULKAN_INSTANCE_FUNCTIONS(VULKAN_LOAD_INSTANCE_FUNCTION)
    VULKAN_DEVICE_FUNCTIONS(VULKAN_LOAD_DEVICE_FUNCTION)
}
