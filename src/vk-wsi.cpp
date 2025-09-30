#include "vk-wsi-impl.hpp"

#include <print>

static
auto* vkwsi_temp(auto&& v)
{
    return &v;
}

template<typename Container, typename Fn, typename... Args>
static
auto vkwsi_enumerate(Container& container, Fn&& fn, Args&&... args)
{
    uint32_t count = static_cast<uint32_t>(container.size());
    for (;;) {
        uint32_t old_count = count;
        if constexpr (std::same_as<VkResult, decltype(fn(args..., &count, nullptr))>) {
            VkResult res = fn(args..., &count, container.data());
            if (res != VK_INCOMPLETE && res != VK_SUCCESS) {
                return res;
            }

            container.resize(count);
            if (count <= old_count) return VK_SUCCESS;
        } else {
            fn(args..., &count, container.data());

            container.resize(count);
            if (count <= old_count) return;
        }
    }
}

#define VKWSI_CHECK(vkwsi_res) if (vkwsi_res != VK_SUCCESS) return res

// -----------------------------------------------------------------------------

// static
// VkResult wait_and_reset_fence(vkwsi_context* ctx, VkFence fence)
// {
//     VkResult res;

//     res = ctx->WaitForFences(ctx->device, 1, &fence, true, UINT64_MAX);
//     VKWSI_CHECK(res);

//     res = ctx->ResetFences(ctx->device, 1, &fence);
//     VKWSI_CHECK(res);

//     return VK_SUCCESS;
// }

// -----------------------------------------------------------------------------

static
VkResult vkwsi_recover_binary_semaphores(vkwsi_context* ctx);

VkResult vkwsi_context_create(vkwsi_context** pp_ctx, const vkwsi_context_info& info)
{
    VkResult res;

    auto ctx = new vkwsi_context {};
    // TODO: Cleanup on error

    if (!info.instance || !info.device || !info.physical_device) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    ctx->instance = info.instance;
    ctx->device = info.device;
    ctx->physical_device = info.physical_device;

    vkwsi_init_functions(ctx, info.instance, info.device, info.get_instance_proc_addr);
    // TODO: Check that required functions have loaded

#if VKWSI_DEBUG_LINEARIZE
    res = ctx->CreateFence(ctx->device, vkwsi_temp(VkFenceCreateInfo {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    }), ctx->alloc, &ctx->debug_fence);
    VKWSI_CHECK(res);
#endif

    res = ctx->CreateSemaphore(ctx->device, vkwsi_temp(VkSemaphoreCreateInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = vkwsi_temp(VkSemaphoreTypeCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue = 0,
        }),
    }), nullptr, &ctx->timeline);
    VKWSI_CHECK(res);

    *pp_ctx = ctx;

    return VK_SUCCESS;
}

void vkwsi_context_destroy(vkwsi_context* ctx)
{
#if VKWSI_DEBUG_LINEARIZE
    ctx->DestroyFence(ctx->device, ctx->debug_fence, ctx->alloc);
#endif

    vkwsi_recover_binary_semaphores(ctx);

    for (auto& sema : ctx->binary_semaphores) {
        ctx->DestroySemaphore(ctx->device, sema, ctx->alloc);
    }

    for (auto fence : ctx->fences) {
        ctx->DestroyFence(ctx->device, fence, ctx->alloc);
    }

    delete ctx;
}

static
VkResult vkwsi_get_fence(vkwsi_context* ctx, VkFence* p_fence)
{
    VkResult res;

    if (ctx->fences.empty()) {
        static uint64_t debug_allocated_count = 0;
        std::println("WARN: Allocated new fence: {}", ++debug_allocated_count);

        VkFence fence;
        res = ctx->CreateFence(ctx->device, vkwsi_temp(VkFenceCreateInfo {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        }), ctx->alloc, &fence);
        VKWSI_CHECK(res);

        *p_fence = fence;
    } else {
        *p_fence = ctx->fences.back();
        ctx->fences.pop_back();
    }

    return VK_SUCCESS;
}

static
VkResult vkwsi_return_fence(vkwsi_context* ctx, VkFence fence)
{
    VkResult res;

    res = ctx->ResetFences(ctx->device, 1, &fence);
    VKWSI_CHECK(res);

    ctx->fences.emplace_back(fence);

    return VK_SUCCESS;
}

static
VkResult vkwsi_get_binary_semaphore(vkwsi_context* ctx, VkSemaphore* p_semaphore)
{
    VkResult res;

    if (ctx->binary_semaphores.empty()) {
        static uint64_t debug_allocated_count = 0;
        std::println("WARN: Allocated new binary sempahore: {}", ++debug_allocated_count);

        VkSemaphore semaphore;
        res = ctx->CreateSemaphore(ctx->device, vkwsi_temp(VkSemaphoreCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        }), ctx->alloc, &semaphore);
        VKWSI_CHECK(res);

        *p_semaphore = semaphore;
    } else {
        // std::println("INFO: Reusing binary semaphore (count = {})", ctx->binary_semaphores.size());
        *p_semaphore = ctx->binary_semaphores.back();
        ctx->binary_semaphores.pop_back();
    }

    return VK_SUCCESS;
}

static
void vkwsi_return_binary_semaphore(vkwsi_context* ctx, VkSemaphore semaphore)
{
    ctx->binary_semaphores.emplace_back(semaphore);
}

static
VkResult vkwsi_recover_binary_semaphores(vkwsi_context* ctx)
{
    VkResult res;

    if (!ctx->acquire_resource_release_queue.empty()) {
        uint64_t current_timeline_value = 0;
        res = ctx->GetSemaphoreCounterValue(ctx->device, ctx->timeline, &current_timeline_value);
        VKWSI_CHECK(res);

        // std::println("current timeline value: {}", current_timeline_value);

        while (!ctx->acquire_resource_release_queue.empty()) {
            auto& head = ctx->acquire_resource_release_queue.front();

            if (current_timeline_value >= head.timeline_value) {
                // std::println("freeing {} semaphores from value {}", head.semaphores.size(), head.timeline_value);
                for (auto& sema : head.semaphores) {
                    vkwsi_return_binary_semaphore(ctx, sema);
                }
                ctx->acquire_resource_release_queue.pop_front();
            } else {
                break;
            }
        }
    }

    return VK_SUCCESS;
}

static
VkResult vkwsi_on_swapchain_present_complete(vkwsi_swapchain* swapchain, uint32_t idx)
{
    auto ctx = swapchain->ctx;
    VkResult res;

    // std::println("\n\nPRESENT {} COMPLETE", idx);

    auto& fence = swapchain->resources[idx].present_signal_fence;
    if (fence) {
        res = vkwsi_return_fence(ctx, fence);
        VKWSI_CHECK(res);
        fence = nullptr;
    }

    auto& sema = swapchain->resources[idx].last_present_wait_semaphore;
    if (sema) {
        // std::println("decrementing present_wait_sema refcount at index {} (current = {})", idx, ctx->present_semaphore_release_map.at(sema));
        if (!--ctx->present_semaphore_release_map.at(sema)) {
            vkwsi_return_binary_semaphore(ctx, sema);
            ctx->present_semaphore_release_map.erase(sema);
        }
        sema = nullptr;
    }

    return VK_SUCCESS;
}

static
VkResult vkwsi_wait_for_present_complete(vkwsi_swapchain* swapchain, uint32_t present_index)
{
    auto ctx = swapchain->ctx;
    VkResult res;

    auto& fence = swapchain->resources[present_index].present_signal_fence;
    if (!fence) {
        return VK_SUCCESS;
    }

    res = ctx->WaitForFences(ctx->device, 1, &fence, true, UINT64_MAX);
    VKWSI_CHECK(res);

    vkwsi_on_swapchain_present_complete(swapchain, present_index);

    return VK_SUCCESS;
}

static
VkResult vkwsi_wait_all_present_complete(vkwsi_swapchain* swapchain)
{
    VkResult res;

    for (uint32_t i = 0; i < swapchain->resources.size(); ++i) {
        res = vkwsi_wait_for_present_complete(swapchain, i);
        VKWSI_CHECK(res);
    }

    return VK_SUCCESS;
}

// -----------------------------------------------------------------------------

VkResult vkwsi_swapchain_create(vkwsi_swapchain** pp_swapchain, vkwsi_context* ctx, VkSurfaceKHR surface)
{
    VkResult res;

    auto swapchain = new vkwsi_swapchain {};

    swapchain->ctx = ctx;
    swapchain->surface = surface;

    *pp_swapchain = swapchain;

    return VK_SUCCESS;
}

void vkwsi_swapchain_set_info(vkwsi_swapchain* swapchain, vkwsi_swapchain_info info)
{
    swapchain->pending_info = std::move(info);
    swapchain->out_of_date = true;
}

static
void vkwsi_destroy_vk_swapchain(vkwsi_swapchain* swapchain, VkSwapchainKHR vk_swapchain)
{
    auto ctx = swapchain->ctx;

    for (auto& res : swapchain->resources) {
        ctx->DestroyImageView(ctx->device, res.view, ctx->alloc);
    }

    ctx->DestroySwapchainKHR(ctx->device, vk_swapchain, ctx->alloc);
}

void vkwsi_swapchain_destroy(vkwsi_swapchain* swapchain)
{
    vkwsi_wait_all_present_complete(swapchain);
    vkwsi_destroy_vk_swapchain(swapchain, swapchain->swapchain);

    delete swapchain;
}

static
VkResult vkwsi_swapchain_recreate(vkwsi_swapchain* swapchain)
{
    auto ctx = swapchain->ctx;
    VkResult res;

    vkwsi_wait_all_present_complete(swapchain);

    auto info = swapchain->pending_info;

    auto desired_extent = swapchain->pending_extent;

    VkSurfaceCapabilities2KHR caps { VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR };
    res = ctx->GetPhysicalDeviceSurfaceCapabilities2KHR(ctx->physical_device, vkwsi_temp(VkPhysicalDeviceSurfaceInfo2KHR {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR,
        .surface = swapchain->surface,
    }), &caps);
    VKWSI_CHECK(res);

    auto& surface_caps = caps.surfaceCapabilities;

    auto extent = VkExtent2D {
        .width = std::clamp(desired_extent.width, surface_caps.minImageExtent.width, surface_caps.maxImageExtent.width),
        .height = std::clamp(desired_extent.height, surface_caps.minImageExtent.height, surface_caps.maxImageExtent.width),
    };

    auto min_image_count = std::max(info.min_image_count, surface_caps.minImageCount);
    if (surface_caps.maxImageCount) min_image_count = std::min(min_image_count, surface_caps.maxImageCount);

    std::println("Recreating swapchain");
    std::println("        min_extent = ({:5}, {:5})", surface_caps.minImageExtent.width, surface_caps.minImageExtent.height);
    if (surface_caps.currentExtent.width == 0xFFFFFFFF && surface_caps.currentExtent.height == 0xFFFFFFFF) {
        std::println("        cur_extent = ( ??? ,  ??? )");
    } else {
        std::println("        cur_extent = ({:5}, {:5})", surface_caps.currentExtent.width, surface_caps.currentExtent.height);
    }
    std::println("        max_extent = ({:5}, {:5})", surface_caps.maxImageExtent.width, surface_caps.maxImageExtent.height);
    std::println("    desired_extent = ({:5}, {:5})", desired_extent.width, desired_extent.height);
    std::println("      final_extent = ({:5}, {:5})", extent.width, extent.height);
    if (surface_caps.maxImageCount) {
        std::println("  caps_image_count = ({}..{})", surface_caps.minImageCount, surface_caps.maxImageCount);
    } else {
        std::println("  caps_image_count = ({}..)", surface_caps.minImageCount);
    }
        std::println("   min_image_count =  {}", info.min_image_count);
        std::println(" final_image_count =  {}", min_image_count);

    auto old_swapchain = swapchain->swapchain;
    res = ctx->CreateSwapchainKHR(ctx->device, vkwsi_temp(VkSwapchainCreateInfoKHR {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        // .flags = VK_SWAPCHAIN_CREATE_DEFERRED_MEMORY_ALLOCATION_BIT_KHR,
        .surface               = swapchain->surface,
        .minImageCount         = info.min_image_count,
        .imageFormat           = info.format,
        .imageColorSpace       = info.color_space,
        .imageExtent           = extent,
        .imageArrayLayers      = info.image_array_layers,
        .imageUsage            = info.image_usage,
        .imageSharingMode      = info.image_sharing_mode,
        .queueFamilyIndexCount = uint32_t(info.queue_families.size()),
        .pQueueFamilyIndices   = info.queue_families.data(),
        .preTransform          = info.pre_transform,
        .compositeAlpha        = info.composite_alpha,
        .presentMode           = info.present_mode,
        .oldSwapchain          = old_swapchain,
    }), ctx->alloc, &swapchain->swapchain);
    VKWSI_CHECK(res);
    // TODO: It seems like there a race-condition between querying surface capabilities and creating the swapchain?
    //       What is the best course of action here?

    vkwsi_destroy_vk_swapchain(swapchain, old_swapchain);

    std::vector<VkImage> images;
    vkwsi_enumerate(images, ctx->GetSwapchainImagesKHR, ctx->device, swapchain->swapchain);

    swapchain->resources.resize(images.size());
    for (uint32_t i = 0; i < images.size(); ++i) {
        swapchain->resources[i] = {
            .image = images[i],
            .view = nullptr,
            .last_present_wait_semaphore = nullptr,
        };
    }

    swapchain->last_extent = extent;
    swapchain->out_of_date = false;
    swapchain->info = info;

    return VK_SUCCESS;
}

VkResult vkwsi_swapchain_resize(vkwsi_swapchain* swapchain, VkExtent2D extent)
{
    if (extent.width != swapchain->last_extent.width || extent.height != swapchain->last_extent.height) {
        swapchain->pending_extent = extent;
        swapchain->out_of_date = true;
        // swapchain->last_extent = extent;
        // return vkwsi_swapchain_recreate(swapchain);
    }

    return VK_SUCCESS;
}

VkResult vkwsi_swapchain_acquire(std::span<vkwsi_swapchain*> swapchains, VkQueue adapter_queue, std::span<const VkSemaphoreSubmitInfo> _signals)
{
    if (swapchains.empty()) return VK_SUCCESS;

    auto ctx = swapchains.front()->ctx;
    VkResult res;

#if VKWSI_DEBUG_LINEARIZE
        VkFence debug_fence = ctx->debug_fence;
#else
        VkFence debug_fence = nullptr;
#endif

    vkwsi_recover_binary_semaphores(ctx);

    std::vector<VkSemaphoreSubmitInfo> wait_infos(swapchains.size());
    for (uint32_t i = 0; i < swapchains.size(); ++i) {
        auto swapchain = swapchains[i];

        if (!swapchain->swapchain || swapchain->out_of_date) vkwsi_swapchain_recreate(swapchain);

        uint32_t image_idx;

        VkSemaphore wait_semaphore = nullptr;
        res = vkwsi_get_binary_semaphore(ctx, &wait_semaphore);
        VKWSI_CHECK(res);

        wait_infos[i] = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = wait_semaphore,
        };

        res = ctx->AcquireNextImageKHR(ctx->device, swapchain->swapchain, UINT64_MAX, wait_semaphore, debug_fence, &image_idx);
        if (res == VK_ERROR_OUT_OF_DATE_KHR) {
            res = vkwsi_swapchain_recreate(swapchain);
            VKWSI_CHECK(res);
            res = ctx->AcquireNextImageKHR(ctx->device, swapchain->swapchain, UINT64_MAX, wait_semaphore, debug_fence, &image_idx);
        }
        if (res != VK_SUBOPTIMAL_KHR) {
            VKWSI_CHECK(res);
        }
#if VKWSI_DEBUG_LINEARIZE
        wait_and_reset_fence(ctx, debug_fence);
#endif
        swapchain->image_index = image_idx;

        vkwsi_wait_for_present_complete(swapchain, image_idx);
        // vkwsi_on_swapchain_present_complete(swapchain, image_idx);

        if (!swapchain->resources[image_idx].view) {
            res = ctx->CreateImageView(ctx->device, vkwsi_temp(VkImageViewCreateInfo {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = swapchain->resources[image_idx].image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = swapchain->info.format,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
            }), ctx->alloc, &swapchain->resources[image_idx].view);
            VKWSI_CHECK(res);
        }
    }

    auto timeline_value = ++ctx->timeline_value;
    std::vector<VkSemaphoreSubmitInfo> signals;
    signals.reserve(_signals.size() + 1);
    signals.append_range(_signals);
    signals.emplace_back(VkSemaphoreSubmitInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = ctx->timeline,
        .value = timeline_value,
    });

    {
        res = ctx->QueueSubmit2(adapter_queue, 1, vkwsi_temp(VkSubmitInfo2 {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .waitSemaphoreInfoCount = uint32_t(swapchains.size()),
            .pWaitSemaphoreInfos = wait_infos.data(),
            .signalSemaphoreInfoCount = uint32_t(signals.size()),
            .pSignalSemaphoreInfos = signals.data(),
        }), debug_fence);
        VKWSI_CHECK(res);

#if VKWSI_DEBUG_LINEARIZE
        wait_and_reset_fence(ctx, debug_fence);
#endif

        auto& resources = ctx->acquire_resource_release_queue.emplace_back();
        resources.timeline_value = timeline_value;
        resources.semaphores.resize(swapchains.size());
        for (uint32_t i = 0; i < swapchains.size(); ++i) {
            resources.semaphores[i] = wait_infos[i].semaphore;
        }
    }

    return VK_SUCCESS;
}

vkwsi_swapchain_image vkwsi_swapchain_get_current(vkwsi_swapchain* swapchain)
{
    return {
        .index = swapchain->image_index,
        .image = swapchain->resources[swapchain->image_index].image,
        .view  = swapchain->resources[swapchain->image_index].view,
        .extent = swapchain->last_extent,
    };
}

VkResult vkwsi_swapchain_present(std::span<vkwsi_swapchain*> swapchains, VkQueue queue, std::span<const VkSemaphoreSubmitInfo> waits, bool host_wait)
{
    if (swapchains.empty()) return VK_SUCCESS;

    auto ctx = swapchains.front()->ctx;
    VkResult res;

    VkSemaphore binary_sema = nullptr;

#if VKWSI_DEBUG_LINEARIZE
        VkFence debug_fence = ctx->debug_fence;
#else
        VkFence debug_fence = nullptr;
#endif

    if (!waits.empty()) {
        if (host_wait) {
            std::vector<VkSemaphore> semaphores;
            std::vector<uint64_t> values;
            for (uint32_t i = 0; i < waits.size(); ++i) {
                semaphores[i] = waits[i].semaphore;
                values[i] = waits[i].value;
            }

            res = ctx->WaitSemaphores(ctx->device, vkwsi_temp(VkSemaphoreWaitInfo {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
                .semaphoreCount = uint32_t(waits.size()),
                .pSemaphores = semaphores.data(),
                .pValues = values.data(),
            }), UINT64_MAX);
        } else {
            res = vkwsi_get_binary_semaphore(ctx, &binary_sema);
            VKWSI_CHECK(res);

            res = ctx->QueueSubmit2(queue, 1, vkwsi_temp(VkSubmitInfo2 {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                .waitSemaphoreInfoCount = uint32_t(waits.size()),
                .pWaitSemaphoreInfos = waits.data(),
                .signalSemaphoreInfoCount = 1,
                .pSignalSemaphoreInfos = vkwsi_temp(VkSemaphoreSubmitInfo {
                    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                    .semaphore = binary_sema,
                }),
            }), debug_fence);
            VKWSI_CHECK(res);

#if VKWSI_DEBUG_LINEARIZE
            wait_and_reset_fence(ctx, debug_fence);
#endif
        }
    }

    std::vector<VkSwapchainKHR> vk_swapchains(swapchains.size());
    std::vector<uint32_t>             indices(swapchains.size());
    std::vector<VkFence>       present_fences(swapchains.size());
    std::vector<VkResult>             results(swapchains.size());
    for (uint32_t i = 0; i < swapchains.size(); ++i) {
        auto& sc = *swapchains[i];
        vk_swapchains[i] = sc.swapchain;
        indices[i] = sc.image_index;

        if (sc.resources[sc.image_index].present_signal_fence) {
            std::println("ERROR: Unexpected unreturned fence at index {}", sc.image_index);
        }

        VkFence fence;
        res = vkwsi_get_fence(ctx, &fence);
        present_fences[i] = fence;
        sc.resources[sc.image_index].present_signal_fence = fence;
        VKWSI_CHECK(res);
    }

    ctx->QueuePresentKHR(queue, vkwsi_temp(VkPresentInfoKHR {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = vkwsi_temp(VkSwapchainPresentFenceInfoKHR {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR,
            .swapchainCount = uint32_t(swapchains.size()),
            .pFences = present_fences.data(),
        }),
        .waitSemaphoreCount = binary_sema ? 1u : 0u,
        .pWaitSemaphores = &binary_sema,
        .swapchainCount = uint32_t(swapchains.size()),
        .pSwapchains = vk_swapchains.data(),
        .pImageIndices = indices.data(),
        .pResults = results.data(),
    }));

    if (binary_sema) {
        ctx->present_semaphore_release_map[binary_sema] = swapchains.size();
        for (auto& swapchain : swapchains) {
            swapchain->resources[swapchain->image_index].last_present_wait_semaphore = binary_sema;
        }
    }

    for (uint32_t i = 0; i < swapchains.size(); ++i) {
        if (results[i] == VK_ERROR_OUT_OF_DATE_KHR) {
            swapchains[i]->out_of_date = true;
            continue;
        }
        if (results[i] == VK_SUBOPTIMAL_KHR) {
            continue;
        }
        VKWSI_CHECK(results[i]);
    }

#if VKWSI_DEBUG_LINEARIZE
    for (uint32_t i = 0; i < swapchains.size(); ++i) {
        res = vkwsi_wait_for_present_complete(swapchains[i], swapchains[i]->image_index);
        VKWSI_CHECK(res);
    }
#endif

    return VK_SUCCESS;
}
