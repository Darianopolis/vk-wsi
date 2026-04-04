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

typedef struct vkwsi_context_info
{
    VkInstance instance;
    VkDevice device;
    VkPhysicalDevice physical_device;
    PFN_vkGetInstanceProcAddr get_instance_proc_addr;
} vkwsi_context_info;

typedef struct vkwsi_context vkwsi_context;

// TODO: Add a function for querying what instance and device extensions / features are required?

VkResult vkwsi_context_create( vkwsi_context**, const vkwsi_context_info*);
void     vkwsi_context_destroy(vkwsi_context*);

VkPresentModeKHR vkwsi_select_present_mode(vkwsi_context*, VkSurfaceKHR, const VkPresentModeKHR*, uint32_t present_mode_count);

typedef struct vkwsi_swapchain vkwsi_swapchain;

typedef struct vkwsi_image vkwsi_image;

VkExtent2D  vkwsi_image_get_extent(vkwsi_image*);
VkImage     vkwsi_image_get_image( vkwsi_image*);
VkImageView vkwsi_image_get_view(  vkwsi_image*);

VkResult vkwsi_swapchain_create( vkwsi_swapchain**, vkwsi_context*, VkSurfaceKHR);
void     vkwsi_swapchain_destroy(vkwsi_swapchain*);

typedef struct vkwsi_acquire_info
{
    VkFormat format;
    VkColorSpaceKHR color_space;
    uint32_t image_array_layers;
    VkImageUsageFlags image_usage;
    VkSurfaceTransformFlagBitsKHR pre_transform;
    VkCompositeAlphaFlagBitsKHR composite_alpha;
    VkPresentModeKHR present_mode;
    VkExtent2D extent;
} vkwsi_acquire_info;

vkwsi_acquire_info vkwsi_acquire_info_default();

VkResult vkwsi_acquire( vkwsi_swapchain*, const vkwsi_acquire_info*, vkwsi_image**);
VkResult vkwsi_transfer(vkwsi_context*, vkwsi_image**, uint32_t image_count, VkQueue, const VkSemaphoreSubmitInfo* signals, uint32_t signal_count);
VkResult vkwsi_present( vkwsi_context*, vkwsi_image**, uint32_t image_count, VkQueue, const VkSemaphoreSubmitInfo* waits,   uint32_t wait_count  );

#ifdef __cplusplus
}
#endif
