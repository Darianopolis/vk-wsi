#pragma once

#include "vk-wsi.h"

#define VKWSI_INSTANCE_FUNCTIONS(DO)             \
    /* Loading */                                \
    DO(GetDeviceProcAddr)                        \
    /* Surface capabiltliies */                  \
    DO(GetPhysicalDeviceSurfaceCapabilities2KHR) \
    DO(GetPhysicalDeviceSurfacePresentModesKHR)  \

#define VKWSI_DEVICE_FUNCTIONS(DO)  \
DO(SetDebugUtilsObjectNameEXT)  \
    /* Debug */                     \
    /* Semaphores */                \
    DO(CreateSemaphore)             \
    DO(WaitSemaphores)              \
    DO(GetSemaphoreCounterValue)    \
    DO(DestroySemaphore)            \
    /* Fences */                    \
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

#define VKWSI_DECLARE_FUNCTION(      funcName, ...) PFN_vk##funcName funcName;
#define VKWSI_LOAD_INSTANCE_FUNCTION(funcName, ...) functions->funcName = (PFN_vk##funcName)functions->GetInstanceProcAddr(instance, "vk"#funcName);
#define VKWSI_LOAD_DEVICE_FUNCTION(  funcName, ...) functions->funcName = (PFN_vk##funcName)functions->GetDeviceProcAddr(  device,   "vk"#funcName);

struct vkwsi_functions
{
    VKWSI_DECLARE_FUNCTION(GetInstanceProcAddr)
    VKWSI_INSTANCE_FUNCTIONS(VKWSI_DECLARE_FUNCTION)
    VKWSI_DEVICE_FUNCTIONS(VKWSI_DECLARE_FUNCTION)
};

inline
void vkwsi_init_functions(vkwsi_functions* functions, VkInstance instance, VkDevice device, PFN_vkGetInstanceProcAddr loadFn)
{
    functions->GetInstanceProcAddr = loadFn;

    VKWSI_INSTANCE_FUNCTIONS(VKWSI_LOAD_INSTANCE_FUNCTION)
    VKWSI_DEVICE_FUNCTIONS(VKWSI_LOAD_DEVICE_FUNCTION)
}
