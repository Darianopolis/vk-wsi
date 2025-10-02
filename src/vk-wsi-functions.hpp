#pragma once

#include "vk-wsi.h"

#define VULKAN_INSTANCE_FUNCTIONS(DO) \
    DO(EnumeratePhysicalDevices) \
    DO(GetPhysicalDeviceProperties2) \
    DO(GetPhysicalDeviceQueueFamilyProperties) \
    DO(CreateDevice) \
    DO(GetDeviceProcAddr) \
    DO(GetPhysicalDeviceSurfaceFormatsKHR) \
    DO(GetPhysicalDeviceSurfaceCapabilities2KHR) \
    DO(DestroySurfaceKHR) \
    DO(DestroyDevice) \
    DO(DestroyInstance) \
    DO(GetPhysicalDeviceSurfacePresentModesKHR)

#define VULKAN_DEVICE_FUNCTIONS(DO) \
    DO(GetDeviceQueue) \
    DO(CreateCommandPool) \
    DO(AllocateCommandBuffers) \
    DO(CreateSemaphore) \
    DO(CreatePipelineLayout) \
    DO(CreateDescriptorPool) \
    DO(CreateGraphicsPipelines) \
    DO(WaitForFences) \
    DO(ResetFences) \
    DO(QueueWaitIdle) \
    DO(DestroyImageView) \
    DO(CreateSwapchainKHR) \
    DO(DestroySwapchainKHR) \
    DO(GetSwapchainImagesKHR) \
    DO(CreateImageView) \
    DO(AcquireNextImageKHR) \
    DO(CmdPipelineBarrier2) \
    DO(BeginCommandBuffer) \
    DO(CmdBeginRendering) \
    DO(CmdSetViewport) \
    DO(CmdSetScissor) \
    DO(CmdBindPipeline) \
    DO(CmdDraw) \
    DO(CmdEndRendering) \
    DO(EndCommandBuffer) \
    DO(QueueSubmit2) \
    DO(QueuePresentKHR) \
    DO(WaitSemaphores) \
    DO(DestroyCommandPool) \
    DO(DestroySemaphore) \
    DO(DestroyPipelineLayout) \
    DO(DestroyPipeline) \
    DO(CreateFence) \
    DO(DestroyFence) \
    DO(DestroyDescriptorPool) \
    DO(CmdClearColorImage) \
    DO(ResetCommandPool) \
    DO(GetSemaphoreCounterValue) \
    DO(SetDebugUtilsObjectNameEXT)

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
