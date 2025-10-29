#pragma once

#include "vk-wsi.h"
#include "vk-wsi-functions.hpp"

#include <deque>
#include <vector>
#include <span>
#include <unordered_map>

#ifndef VKWSI_DEBUG_LINEARIZE
# define VKWSI_DEBUG_LINEARIZE 0
#endif

#ifndef VKWSI_NOISY_SWAPCHAIN_CREATION
# define VKWSI_NOISY_SWAPCHAIN_CREATION 0
#endif

#define VKWSI_CONCAT_INTERNAL(a, b) a##b
#define VKWSI_CONCAT(a, b) VKWSI_CONCAT_INTERNAL(a, b)
#define VKWSI_UNQIUE_VAR() VKWSI_CONCAT(vkwsi_var_, __COUNTER__)

template<typename fn_t>
struct vkwsi_defer_guard
{
    fn_t fn;

    vkwsi_defer_guard(fn_t&& fn): fn(std::move(fn)) {}
    ~vkwsi_defer_guard() { fn(); };
};

#define defer vkwsi_defer_guard VKWSI_UNQIUE_VAR() = [&]

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

    vkwsi_log_callback log_callback = {};

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
    uint64_t version = 0;

    vkwsi_swapchain_info info = {};
    vkwsi_swapchain_info pending_info = {};
};
