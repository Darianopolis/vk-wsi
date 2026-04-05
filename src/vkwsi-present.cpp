#include "vkwsi.hpp"
#include "vkwsi-util.hpp"

#include <iostream>
#include <format>

VkResult vkwsi_transfer(vkwsi_context* ctx, const vkwsi_transfer_info* transfers, uint32_t image_count, vkwsi_queue queue, const VkSemaphoreSubmitInfo* signals, uint32_t signal_count)
{
    // Transition

    auto commands = vkwsi_begin_commands(ctx, queue);
    VKWSI_CHECK(commands.result);

    for (uint32_t i = 0; i < image_count; ++i) {
        ctx->vk.CmdPipelineBarrier2(commands.buffer, vkwsi_ptr_to(VkDependencyInfo {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = vkwsi_ptr_to(VkImageMemoryBarrier2 {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
                .oldLayout = transfers[i].image->current_layout,
                .newLayout = transfers[i].layout,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = transfers[i].image->image,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
            }),
        }));
        transfers[i].image->current_layout = transfers[i].layout;
    }

    VKWSI_CHECK(vkwsi_submit_commands(ctx, queue, commands, signals, signal_count));

    return VK_SUCCESS;
}

VkResult vkwsi_present(vkwsi_context* ctx, const vkwsi_transfer_info* transfers, uint32_t image_count, vkwsi_queue queue, const VkSemaphoreSubmitInfo* waits, uint32_t wait_count)
{
    if (!image_count) return VK_SUCCESS;

    // Wait

    if (wait_count) {
        std::vector<VkSemaphore> semaphores(wait_count);
        std::vector<uint64_t>    values(    wait_count);

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

    // Transition

    auto commands = vkwsi_begin_commands(ctx, queue);
    VKWSI_CHECK(commands.result);

    for (uint32_t i = 0; i < image_count; ++i) {
        ctx->vk.CmdPipelineBarrier2(commands.buffer, vkwsi_ptr_to(VkDependencyInfo {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = vkwsi_ptr_to(VkImageMemoryBarrier2 {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
                .oldLayout = transfers[i].layout,
                .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = transfers[i].image->image,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
            }),
        }));
        transfers[i].image->current_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    VKWSI_CHECK(vkwsi_submit_commands(ctx, queue, commands, nullptr, 0));

    // Present

    std::vector<VkFence>        fences(    image_count);
    std::vector<VkSwapchainKHR> swapchains(image_count);
    std::vector<uint32_t>       indices(   image_count);
    std::vector<VkResult>       results(   image_count);

    for (uint32_t i = 0; i < image_count; ++i) {
        swapchains[i] = transfers[i].image->swapchain->swapchain;
        indices[i]    = transfers[i].image->index;

        VKWSI_CHECK(ctx->vk.CreateFence(ctx->device, vkwsi_ptr_to(VkFenceCreateInfo {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        }), ctx->alloc, &fences[i]));
    }

    VKWSI_DEFER {
        for (auto fence : fences) {
            ctx->vk.DestroyFence(ctx->device, fence, ctx->alloc);
        }
    };

    ctx->vk.QueuePresentKHR(queue.handle, vkwsi_ptr_to(VkPresentInfoKHR {
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
        transfers[i].image->present_result = results[i];
        if (results[i] == VK_SUBOPTIMAL_KHR || res == VK_ERROR_OUT_OF_DATE_KHR) {
            transfers[i].image->swapchain->out_of_date = true;
        } else if (res != VK_SUCCESS) {
            res = results[i];
            fences[i] = nullptr;
        }
    }

    std::erase(fences, nullptr);
    VKWSI_CHECK(ctx->vk.WaitForFences(ctx->device, fences.size(), fences.data(), true, UINT64_MAX));

    return res;
}
