#pragma once

#include "vkwsi.h"

#include <vector>

// -----------------------------------------------------------------------------

#define VKWSI_INSTANCE_FUNCTIONS(DO)             \
    /* Loading */                                \
    DO(GetDeviceProcAddr)                        \
    /* Surface capabiltliies */                  \
    DO(GetPhysicalDeviceSurfaceCapabilities2KHR) \
    DO(GetPhysicalDeviceSurfacePresentModesKHR)  \

#define VKWSI_DEVICE_FUNCTIONS(DO)  \
    /* Semaphores */                \
    DO(CreateSemaphore)             \
    DO(WaitSemaphores)              \
    DO(GetSemaphoreCounterValue)    \
    DO(SignalSemaphore)             \
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

#define VKWSI_DECLARE_FUNCTION(      Func, ...) PFN_vk##Func Func;
#define VKWSI_LOAD_INSTANCE_FUNCTION(Func, ...) functions->Func = (PFN_vk##Func)functions->GetInstanceProcAddr(instance, "vk"#Func);
#define VKWSI_LOAD_DEVICE_FUNCTION(  Func, ...) functions->Func = (PFN_vk##Func)functions->GetDeviceProcAddr(  device,   "vk"#Func);

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

// -----------------------------------------------------------------------------

struct vkwsi_context
{
    vkwsi_functions vk;

    VkInstance instance = {};
    VkDevice device = {};
    VkPhysicalDevice physical_device;
    const VkAllocationCallbacks* alloc = {};
};

struct vkwsi_image
{
    vkwsi_swapchain* swapchain;

    VkImage image;
    VkImageView view;

    uint32_t index;
    VkExtent2D extent;

    VkResult present_result;
};

struct vkwsi_swapchain
{
    vkwsi_context* ctx = {};
    VkSurfaceKHR surface = {};
    VkSwapchainKHR swapchain = {};

    vkwsi_acquire_info info = {};
    bool out_of_date = true;

    std::vector<vkwsi_image> images;
};

void vkwsi_swapchain_destroy_resources(vkwsi_swapchain*);
