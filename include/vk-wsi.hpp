#pragma once

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "vk-wsi-functions.hpp"

#include <deque>
#include <vector>
#include <span>
#include <unordered_map>

#define VKWSI_DEBUG_LINEARIZE 0

struct vkwsi_context_info
{
    VkInstance instance;
    VkDevice device;
    VkPhysicalDevice physical_device;
    PFN_vkGetInstanceProcAddr get_instance_proc_addr;
};

struct vkwsi_acquire_resources
{
    uint64_t timeline_value;
    std::vector<VkSemaphore> semaphores;
};

struct vkwsi_context : vkwsi_functions
{
    VkInstance instance = {};
    VkDevice device = {};
    VkPhysicalDevice physical_device;
    const VkAllocationCallbacks* alloc = {};

#if VKWSI_DEBUG_LINEARIZE
    VkFence debug_fence = {};
#endif

    VkSemaphore timeline = {};
    uint64_t timeline_value = 0;

    std::vector<VkFence> fences;
    std::vector<VkSemaphore> binary_semaphores;

    std::deque<vkwsi_acquire_resources> acquire_resource_release_queue;
    std::unordered_map<VkSemaphore, uint32_t> present_semaphore_release_map;
};

VkResult vkwsi_context_init(vkwsi_context* ctx, const vkwsi_context_info& info);
void vkwsi_context_destroy(vkwsi_context* ctx);

struct vkwsi_swapchain_info
{
    uint32_t min_image_count;

    VkFormat format;
    VkColorSpaceKHR color_space;

    // extent handled separately
    uint32_t image_array_layers = 1;

    VkImageUsageFlags image_usage;

    VkSharingMode image_sharing_mode;
    std::vector<uint32_t> queue_families;

    VkSurfaceTransformFlagBitsKHR pre_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
};

struct vkwsi_swapchain_per_image_resources
{
    VkImage image;
    VkImageView view;
    VkFence present_signal_fence;
    VkSemaphore last_present_wait_semaphore;
};

struct vkwsi_swapchain
{
    vkwsi_context* ctx = {};
    VkSurfaceKHR surface = {};
    VkSwapchainKHR swapchain = {};
    VkExtent2D last_extent = {};
    VkExtent2D pending_extent = {};

    std::vector<vkwsi_swapchain_per_image_resources> resources;
    uint32_t image_index;

    bool out_of_date = true;

    vkwsi_swapchain_info info = {};
};

struct vkwsi_swapchain_image
{
    uint32_t index;
    VkImage image;
    VkImageView view;
    VkExtent2D extent;
};

VkResult vkwsi_swapchain_init(vkwsi_swapchain* swapchain, vkwsi_context* ctx, VkSurfaceKHR surface);
void vkwsi_swapchain_destroy(vkwsi_swapchain* swapchain);
VkResult vkwsi_swapchain_resize(vkwsi_swapchain* swapchain, VkExtent2D extent);
VkResult vkwsi_swapchain_acquire(std::span<vkwsi_swapchain*>, VkQueue adapter_queue, std::span<const VkSemaphoreSubmitInfo> signals);
vkwsi_swapchain_image vkwsi_swapchain_get_current(vkwsi_swapchain* swapchain);
VkResult vkwsi_swapchain_present(std::span<vkwsi_swapchain*> swapchains, VkQueue adapter_queue, std::span<const VkSemaphoreSubmitInfo> waits, bool host_wait);
