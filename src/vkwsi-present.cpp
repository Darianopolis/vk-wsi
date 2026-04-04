#include "vkwsi.hpp"
#include "vkwsi-util.hpp"

VkResult vkwsi_transfer(vkwsi_context* ctx, vkwsi_image** images, uint32_t image_count, VkQueue, const VkSemaphoreSubmitInfo* signals, uint32_t signal_count)
{
    for (uint32_t i = 0; i < signal_count; ++i) {
        VKWSI_CHECK(ctx->vk.SignalSemaphore(ctx->device, vkwsi_ptr_to(VkSemaphoreSignalInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
            .semaphore = signals[i].semaphore,
            .value = signals[i].value
        })));
    }

    return VK_SUCCESS;
}

VkResult vkwsi_present(vkwsi_context* ctx, vkwsi_image** images, uint32_t image_count, VkQueue queue, const VkSemaphoreSubmitInfo* waits, uint32_t wait_count)
{
    if (!image_count) return VK_SUCCESS;

    if (wait_count) {
        std::vector<VkSemaphore> semaphores(wait_count);
        std::vector<uint64_t>    values(wait_count);
        for (uint32_t i = 0; i < wait_count; ++i) {
            semaphores[i] = waits[i].semaphore;
            values[i]     = waits[i].value;
        }

        VKWSI_CHECK(ctx->vk.WaitSemaphores(ctx->device, vkwsi_ptr_to(VkSemaphoreWaitInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
            .semaphoreCount = uint32_t(wait_count),
            .pSemaphores = semaphores.data(),
            .pValues = values.data(),
        }), UINT64_MAX));
    }

    std::vector<VkFence> fences(image_count);
    std::vector<VkSwapchainKHR> swapchains(image_count);
    std::vector<uint32_t>       indices(image_count);
    for (uint32_t i = 0; i < image_count; ++i) {
        swapchains[i] = images[i]->swapchain->swapchain;
        indices[i]    = images[i]->index;

        VKWSI_CHECK(ctx->vk.CreateFence(ctx->device, vkwsi_ptr_to(VkFenceCreateInfo {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        }), ctx->alloc, &fences[i]));
    }
    VKWSI_DEFER {
        for (auto fence : fences) {
            ctx->vk.DestroyFence(ctx->device, fence, ctx->alloc);
        }
    };

    std::vector<VkResult> results(image_count);
    ctx->vk.QueuePresentKHR(queue, vkwsi_ptr_to(VkPresentInfoKHR {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = vkwsi_ptr_to(VkSwapchainPresentFenceInfoKHR {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR,
            .swapchainCount = image_count,
            .pFences = fences.data(),
        }),
        .swapchainCount = image_count,
        .pSwapchains = swapchains.data(),
        .pImageIndices = indices.data(),
        .pResults = results.data(),
    }));

    VkResult res = VK_SUCCESS;
    for (uint32_t i = 0; i < image_count; ++i) {
        images[i]->present_result = results[i];
        if (results[i] == VK_SUBOPTIMAL_KHR || res == VK_ERROR_OUT_OF_DATE_KHR) {
            images[i]->swapchain->out_of_date = true;
        } else {
            res = results[i];
        }
    }

    VKWSI_CHECK(ctx->vk.WaitForFences(ctx->device, image_count, fences.data(), true, UINT64_MAX));

    return res;
}
