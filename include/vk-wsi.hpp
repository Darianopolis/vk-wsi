#pragma once

#ifndef VULKAN_H_
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#endif

#include <vector>
#include <span>

struct vkwsi_context_info
{
    VkInstance instance;
    VkDevice device;
    VkPhysicalDevice physical_device;
    PFN_vkGetInstanceProcAddr get_instance_proc_addr;
};

struct vkwsi_context;

VkResult vkwsi_context_create(vkwsi_context** ctx, const vkwsi_context_info& info);
void     vkwsi_context_destroy(vkwsi_context* ctx);

struct vkwsi_swapchain_info
{
    uint32_t min_image_count;

    VkFormat format;
    VkColorSpaceKHR color_space;

    uint32_t image_array_layers = 1;

    VkImageUsageFlags image_usage;

    VkSharingMode image_sharing_mode;
    std::vector<uint32_t> queue_families;

    VkSurfaceTransformFlagBitsKHR pre_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
};

struct vkwsi_swapchain;

struct vkwsi_swapchain_image
{
    uint32_t index;
    VkImage image;
    VkImageView view;
    VkExtent2D extent;
};

VkResult              vkwsi_swapchain_create(vkwsi_swapchain** swapchain, vkwsi_context* ctx, VkSurfaceKHR surface);
void                  vkwsi_swapchain_destroy(vkwsi_swapchain* swapchain);
void                  vkwsi_swapchain_set_info(vkwsi_swapchain* swapchain, vkwsi_swapchain_info info);
VkResult              vkwsi_swapchain_resize(vkwsi_swapchain* swapchain, VkExtent2D extent);
VkResult              vkwsi_swapchain_acquire(std::span<vkwsi_swapchain*>, VkQueue adapter_queue, std::span<const VkSemaphoreSubmitInfo> signals);
vkwsi_swapchain_image vkwsi_swapchain_get_current(vkwsi_swapchain* swapchain);
VkResult              vkwsi_swapchain_present(std::span<vkwsi_swapchain*> swapchains, VkQueue adapter_queue, std::span<const VkSemaphoreSubmitInfo> waits, bool host_wait);
