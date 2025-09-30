#pragma once

#include "vk-wsi.hpp"
#include "vk-wsi-functions.hpp"

#include <deque>
#include <vector>
#include <span>
#include <unordered_map>

#define VKWSI_DEBUG_LINEARIZE 0

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
    vkwsi_swapchain_info pending_info = {};
};
