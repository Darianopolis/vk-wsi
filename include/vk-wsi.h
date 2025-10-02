#pragma once

#ifndef VULKAN_H_
# ifndef VK_NO_PROTOTYPES
#  define VK_NO_PROTOTYPES
# endif
# include <vulkan/vulkan.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// TODO: Documentation comments

typedef struct vkwsi_context_info
{
    VkInstance instance;
    VkDevice device;
    VkPhysicalDevice physical_device;
    PFN_vkGetInstanceProcAddr get_instance_proc_addr;
} vkwsi_context_info;

typedef struct vkwsi_context vkwsi_context;

// TODO: Add a function for querying what instance and device extensions / features are required?

VkResult vkwsi_context_create(vkwsi_context** ctx, const vkwsi_context_info* info);
void     vkwsi_context_destroy(vkwsi_context* ctx);

VkPresentModeKHR vkwsi_context_pick_present_mode(vkwsi_context* ctx, VkSurfaceKHR surface, const VkPresentModeKHR* present_modes, uint32_t present_mode_count);

// TODO: Handle `queue_families` lifetime management (currently we require it to remain alive for the entire lifetime of the swapchain!)

typedef struct vkwsi_swapchain_info
{
    uint32_t min_image_count;

    VkFormat format;
    VkColorSpaceKHR color_space;

    uint32_t image_array_layers;

    VkImageUsageFlags image_usage;

    VkSharingMode image_sharing_mode;
    const uint32_t* queue_families;
    uint32_t queue_family_count;

    VkSurfaceTransformFlagBitsKHR pre_transform;
    VkCompositeAlphaFlagBitsKHR composite_alpha;

    VkPresentModeKHR present_mode;
} vkwsi_swapchain_info;

vkwsi_swapchain_info vkwsi_swapchain_info_default();

typedef struct vkwsi_swapchain vkwsi_swapchain;

typedef struct vkwsi_swapchain_image
{
    uint32_t index;
    VkImage image;
    VkImageView view;
    VkExtent2D extent;
    uint64_t version;
} vkwsi_swapchain_image;

VkResult              vkwsi_swapchain_create(vkwsi_swapchain** swapchain, vkwsi_context* ctx, VkSurfaceKHR surface);
void                  vkwsi_swapchain_destroy(vkwsi_swapchain* swapchain);
void                  vkwsi_swapchain_set_info(vkwsi_swapchain* swapchain, const vkwsi_swapchain_info* info);
VkResult              vkwsi_swapchain_resize(vkwsi_swapchain* swapchain, VkExtent2D extent);
VkResult              vkwsi_swapchain_acquire(vkwsi_swapchain* const* swapchains, uint32_t swapchain_count, VkQueue adapter_queue, const VkSemaphoreSubmitInfo* signals, uint32_t signal_count);
vkwsi_swapchain_image vkwsi_swapchain_get_current(vkwsi_swapchain* swapchain);
VkResult              vkwsi_swapchain_present(vkwsi_swapchain* const* swapchains, uint32_t swapchain_count, VkQueue queue, const VkSemaphoreSubmitInfo* waits, uint32_t wait_count, bool host_wait);

#ifdef __cplusplus
}
#endif
