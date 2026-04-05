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

vkwsi_commands vkwsi_begin_commands(vkwsi_context* ctx, vkwsi_queue queue)
{
    VkCommandPool pool;
    auto res = ctx->vk.CreateCommandPool(ctx->device, vkwsi_ptr_to(VkCommandPoolCreateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = queue.family,
    }), ctx->alloc, &pool);
    if (res != VK_SUCCESS) return {res};

    VKWSI_DEFER { ctx->vk.DestroyCommandPool(ctx->device, pool, ctx->alloc); };

    VkCommandBuffer buffer;
    res = ctx->vk.AllocateCommandBuffers(ctx->device, vkwsi_ptr_to(VkCommandBufferAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    }), &buffer);
    if (res != VK_SUCCESS) return {res};

    res = ctx->vk.BeginCommandBuffer(buffer, vkwsi_ptr_to(VkCommandBufferBeginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    }));
    if (res != VK_SUCCESS) return {res};

    return {VK_SUCCESS, std::exchange(pool, nullptr), buffer};
}

VkResult vkwsi_submit_commands(vkwsi_context* ctx, vkwsi_queue queue, vkwsi_commands commands, const VkSemaphoreSubmitInfo* signals, uint32_t signal_count)
{
    VKWSI_DEFER { ctx->vk.DestroyCommandPool(ctx->device, commands.pool, ctx->alloc); };

    VkFence fence = nullptr;
    VKWSI_CHECK(ctx->vk.CreateFence(ctx->device, vkwsi_ptr_to(VkFenceCreateInfo {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    }), ctx->alloc, &fence));
    VKWSI_DEFER { ctx->vk.DestroyFence(ctx->device, fence, ctx->alloc); };

    VKWSI_CHECK(ctx->vk.EndCommandBuffer(commands.buffer));

    VKWSI_CHECK(ctx->vk.QueueSubmit2(queue.handle, 1, vkwsi_ptr_to(VkSubmitInfo2 {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = vkwsi_ptr_to(VkCommandBufferSubmitInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = commands.buffer,
        }),
        .signalSemaphoreInfoCount = signal_count,
        .pSignalSemaphoreInfos = signals,
    }), fence));

    VKWSI_CHECK(ctx->vk.WaitForFences(ctx->device, 1, &fence, true, UINT64_MAX));

    return VK_SUCCESS;
}
