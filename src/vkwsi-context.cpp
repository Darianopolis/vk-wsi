#include "vkwsi.hpp"
#include "vkwsi-util.hpp"

#include <algorithm>

VkResult vkwsi_context_create(vkwsi_context** out_ctx, const vkwsi_context_info* info)
{
    auto ctx = new vkwsi_context {};
    VKWSI_DEFER { if (ctx) vkwsi_context_destroy(ctx); };

    if (!info->instance || !info->device || !info->physical_device || !info->get_instance_proc_addr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    ctx->instance = info->instance;
    ctx->device = info->device;
    ctx->physical_device = info->physical_device;

    vkwsi_init_functions(&ctx->vk, info->instance, info->device, info->get_instance_proc_addr);
    // TODO: Check that required functions have loaded

    *out_ctx = std::exchange(ctx, nullptr);
    return VK_SUCCESS;
}

void vkwsi_context_destroy(vkwsi_context* ctx)
{
    delete ctx;
}

VkPresentModeKHR vkwsi_select_present_mode(vkwsi_context* ctx, VkSurfaceKHR surface, const VkPresentModeKHR* present_modes, uint32_t present_mode_count)
{
    std::vector<VkPresentModeKHR> available_present_modes;
    vkwsi_enumerate(available_present_modes, ctx->vk.GetPhysicalDeviceSurfacePresentModesKHR, ctx->physical_device, surface);

    for (uint32_t i = 0; i < present_mode_count; ++i) {
        auto mode = present_modes[i];
        auto begin = available_present_modes.begin();
        auto end = available_present_modes.end();
        if (std::find(begin, end, mode) != end) {
            return mode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkResult vkwsi_swapchain_create(vkwsi_swapchain** out_swapchain, vkwsi_context* ctx, VkSurfaceKHR surface)
{
    auto swapchain = new vkwsi_swapchain {};
    swapchain->ctx = ctx;
    swapchain->surface = surface;

    *out_swapchain = swapchain;
    return VK_SUCCESS;
}

void vkwsi_swapchain_destroy(vkwsi_swapchain* swapchain)
{
    vkwsi_swapchain_destroy_resources(swapchain);

    delete swapchain;
}
