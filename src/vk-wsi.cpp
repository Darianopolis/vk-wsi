#include "vk-wsi-impl.hpp"

#include <iostream>
#include <format>
#include <utility>
#include <concepts>
#include <algorithm>

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

template<typename ...Args>
void vkwsi_log(std::format_string<Args...> fmt, Args&&... args)
{
    std::cout << std::vformat(fmt.get(), std::make_format_args(args...)) << '\n';
}

#define VKWSI_CHECK(vkwsi_res) if (vkwsi_res != VK_SUCCESS) return res

// -----------------------------------------------------------------------------

#if VKWSI_DEBUG_LINEARIZE
static
VkResult vkwsi_h_wait_and_reset_fence(vkwsi_context* ctx, VkFence fence)
{
    VkResult res;

    res = ctx->WaitForFences(ctx->device, 1, &fence, true, UINT64_MAX);
    VKWSI_CHECK(res);

    res = ctx->ResetFences(ctx->device, 1, &fence);
    VKWSI_CHECK(res);

    return VK_SUCCESS;
}
#endif

// -----------------------------------------------------------------------------

vkwsi_swapchain_info vkwsi_swapchain_info_default()
{
    return {
        .min_image_count = 1,

        .format = {},
        .color_space = {},

        .image_array_layers = 1,
        .image_usage = {},

        .image_sharing_mode = VK_SHARING_MODE_EXCLUSIVE,
        .queue_families = {},
        .queue_family_count = {},

        .pre_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,

        .present_mode = VK_PRESENT_MODE_FIFO_KHR,
    };
}

static
VkResult vkwsi_recover_binary_semaphores(vkwsi_context* ctx);

VkResult vkwsi_context_create(vkwsi_context** pp_ctx, const vkwsi_context_info* info)
{
    VkResult res;

    auto ctx = new vkwsi_context {};
    // TODO: Cleanup on error

    if (!info->instance || !info->device || !info->physical_device) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    ctx->instance = info->instance;
    ctx->device = info->device;
    ctx->physical_device = info->physical_device;

    vkwsi_init_functions(ctx, info->instance, info->device, info->get_instance_proc_addr);
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

    ctx->DestroySemaphore(ctx->device, ctx->timeline, ctx->alloc);

    delete ctx;
}

VkPresentModeKHR vkwsi_context_pick_present_mode(vkwsi_context* ctx, VkSurfaceKHR surface, const VkPresentModeKHR* present_modes, uint32_t present_mode_count)
{
    VkResult res;

    auto present_mode_to_string = [](VkPresentModeKHR pm) {
        switch (pm) {
            case VK_PRESENT_MODE_IMMEDIATE_KHR: return "IMMEDIATE";
            case VK_PRESENT_MODE_MAILBOX_KHR: return "MAILBOX";
            case VK_PRESENT_MODE_FIFO_KHR: return "FIFO";
            case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO_RELAXED";
            case VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR: return "SHARED_DEMAND_REFRESH";
            case VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR: return "SHARED_CONTINUOUS_REFRESH";
            case VK_PRESENT_MODE_FIFO_LATEST_READY_KHR: return "FIFO_LATEST_READY";
            default: return "?";
        }
    };

    std::vector<VkPresentModeKHR> available_present_modes;
    res = vkwsi_enumerate(available_present_modes, ctx->GetPhysicalDeviceSurfacePresentModesKHR, ctx->physical_device, surface);
    vkwsi_log("AVAILABLE PRESENT MODES:");
    for (auto pm : available_present_modes) {
        vkwsi_log(" - {}", present_mode_to_string(pm));
    }

    for (uint32_t i = 0; i < present_mode_count; ++i) {
        auto pm = present_modes[i];
        vkwsi_log("CHECKING PRESENT MODE: {}", present_mode_to_string(pm));
        auto begin = available_present_modes.begin();
        auto end = available_present_modes.end();
        if (std::find(begin, end, pm) != end) {
            vkwsi_log("  AVAILABLE!");
            return pm;
        }
    }

    vkwsi_log("FALLING BACK TO FIFO PRESENT MODE");

    return VK_PRESENT_MODE_FIFO_KHR;
}

static
VkResult vkwsi_get_fence(vkwsi_context* ctx, VkFence* p_fence)
{
    VkResult res;

    if (ctx->fences.empty()) {
        static uint64_t debug_allocated_count = 0;
        vkwsi_log("WARN: Allocated new fence: {}", ++debug_allocated_count);

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
        // TODO: Separate debug tracking for acquire and present semaphores
        static uint64_t debug_allocated_count = 0;
        vkwsi_log("WARN: Allocated new binary sempahore: {}", ++debug_allocated_count);

        VkSemaphore semaphore;
        res = ctx->CreateSemaphore(ctx->device, vkwsi_temp(VkSemaphoreCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        }), ctx->alloc, &semaphore);
        VKWSI_CHECK(res);

        *p_semaphore = semaphore;
    } else {
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

        while (!ctx->acquire_resource_release_queue.empty()) {
            auto& head = ctx->acquire_resource_release_queue.front();

            if (current_timeline_value >= head.timeline_value) {
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

    auto& fence = swapchain->resources[idx].present_signal_fence;
    if (fence) {
        res = vkwsi_return_fence(ctx, fence);
        VKWSI_CHECK(res);
        fence = nullptr;
    }

    auto& sema = swapchain->resources[idx].last_present_wait_semaphore;
    if (sema) {
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
    swapchain->out_of_date = true;

    *pp_swapchain = swapchain;

    return VK_SUCCESS;
}

void vkwsi_swapchain_set_info(vkwsi_swapchain* swapchain, const vkwsi_swapchain_info* info)
{
    swapchain->pending_info = *info;
    swapchain->out_of_date = true;
}

static
void vkwsi_destroy_vk_swapchain(vkwsi_swapchain* swapchain)
{
    auto ctx = swapchain->ctx;

    for (auto& res : swapchain->resources) {
        ctx->DestroyImageView(ctx->device, res.view, ctx->alloc);
    }

    ctx->DestroySwapchainKHR(ctx->device, swapchain->swapchain, ctx->alloc);
}

void vkwsi_swapchain_destroy(vkwsi_swapchain* swapchain)
{
    vkwsi_wait_all_present_complete(swapchain);
    vkwsi_destroy_vk_swapchain(swapchain);

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

    VkSurfacePresentScalingCapabilitiesEXT scaling_caps {
        .sType = VK_STRUCTURE_TYPE_SURFACE_PRESENT_SCALING_CAPABILITIES_EXT,
    };

    VkSurfaceCapabilities2KHR caps {
        .sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR,
        .pNext = &scaling_caps,
    };

    res = ctx->GetPhysicalDeviceSurfaceCapabilities2KHR(ctx->physical_device, vkwsi_temp(VkPhysicalDeviceSurfaceInfo2KHR {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR,
        .pNext = vkwsi_temp(VkSurfacePresentModeKHR {
            .sType = VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_KHR,
            .presentMode = swapchain->pending_info.present_mode,
        }),
        .surface = swapchain->surface,
    }), &caps);
    VKWSI_CHECK(res);

    auto& surface_caps = caps.surfaceCapabilities;

#if VKWSI_NOISY_SWAPCHAIN_CREATION
    vkwsi_log("Recreating swapchain");
    vkwsi_log("        min_extent = ({:5}, {:5})", surface_caps.minImageExtent.width, surface_caps.minImageExtent.height);
    if (surface_caps.currentExtent.width == 0xFFFFFFFF && surface_caps.currentExtent.height == 0xFFFFFFFF) {
        vkwsi_log("        cur_extent = ( ??? ,  ??? )");
    } else {
        vkwsi_log("        cur_extent = ({:5}, {:5})", surface_caps.currentExtent.width, surface_caps.currentExtent.height);
    }
    vkwsi_log("        max_extent = ({:5}, {:5})", surface_caps.maxImageExtent.width, surface_caps.maxImageExtent.height);
    vkwsi_log("    desired_extent = ({:5}, {:5})", desired_extent.width, desired_extent.height);
#endif

    auto extent = VkExtent2D {
        .width = std::clamp(desired_extent.width, surface_caps.minImageExtent.width, surface_caps.maxImageExtent.width),
        .height = std::clamp(desired_extent.height, surface_caps.minImageExtent.height, surface_caps.maxImageExtent.height),
    };

    VkPresentScalingFlagsKHR scaling_mode = {};
    if (scaling_caps.supportedPresentScaling) {
        auto min = scaling_caps.minScaledImageExtent;
        auto max = scaling_caps.maxScaledImageExtent;
#if VKWSI_NOISY_SWAPCHAIN_CREATION
        vkwsi_log("      scaling_caps = ({}, {}) -- ({}, {})", min.width, min.height, max.width, max.height);
#endif

        auto scaled_width = std::clamp(desired_extent.width, min.width, max.width);
        auto scaled_height = std::clamp(desired_extent.height, min.height, max.height);
        if (scaled_width == desired_extent.width && scaled_height == desired_extent.height) {

            if (scaling_caps.supportedPresentScaling & VK_PRESENT_SCALING_ONE_TO_ONE_BIT_EXT) {
                scaling_mode = VK_PRESENT_SCALING_ONE_TO_ONE_BIT_EXT;
#if VKWSI_NOISY_SWAPCHAIN_CREATION
                vkwsi_log("      scaling_mode = VK_PRESENT_SCALING_ONE_TO_ONE_BIT_EXT");
#endif
            } else if (scaling_caps.supportedPresentScaling & VK_PRESENT_SCALING_ASPECT_RATIO_STRETCH_BIT_EXT) {
                scaling_mode = VK_PRESENT_SCALING_ASPECT_RATIO_STRETCH_BIT_EXT;
#if VKWSI_NOISY_SWAPCHAIN_CREATION
                vkwsi_log("      scaling_mode = VK_PRESENT_SCALING_ASPECT_RATIO_STRETCH_BIT_EXT");
#endif
            } else if (scaling_caps.supportedPresentScaling & VK_PRESENT_SCALING_STRETCH_BIT_EXT) {
                scaling_mode = VK_PRESENT_SCALING_STRETCH_BIT_EXT;
#if VKWSI_NOISY_SWAPCHAIN_CREATION
                vkwsi_log("      scaling_mode = VK_PRESENT_SCALING_STRETCH_BIT_EXT");
#endif
            } else if (scaling_caps.supportedPresentScaling) {
                // Fallback to selecting the "first" available scaling mode if we don't recognize any
                scaling_mode = VkPresentScalingFlagBitsEXT(1 << std::countr_zero(scaling_caps.supportedPresentScaling));
#if VKWSI_NOISY_SWAPCHAIN_CREATION
                vkwsi_log("      scaling_mode = {}", scaling_mode);
#endif
            }

            if (scaling_mode) {
                extent = { scaled_width, scaled_height };
            }
        }

        VkPresentScalingFlagsEXT flags;
    }

#if VKWSI_NOISY_SWAPCHAIN_CREATION
    vkwsi_log("      final_extent = ({:5}, {:5})", extent.width, extent.height);

    if (surface_caps.maxImageCount) {
        vkwsi_log("  caps_image_count = ({}..{})", surface_caps.minImageCount, surface_caps.maxImageCount);
    } else {
        vkwsi_log("  caps_image_count = ({}..)", surface_caps.minImageCount);
    }
    vkwsi_log("   min_image_count =  {}", info.min_image_count);
#endif

    auto min_image_count = std::max(info.min_image_count, surface_caps.minImageCount);
    if (surface_caps.maxImageCount) min_image_count = std::min(min_image_count, surface_caps.maxImageCount);

#if VKWSI_NOISY_SWAPCHAIN_CREATION
    vkwsi_log(" final_image_count =  {}", min_image_count);
#endif

    // TODO: There is an inherent race condition between querying surface capabilities and creating the swapchain
    //       We need to identify such error conditions continously attempt to recreate the swapchain until we succeed.
    //       Note that acquisition also races, so we need to loop both until we successfully acquire an image.

    VkSwapchainKHR new_swapchain = {};
    res = ctx->CreateSwapchainKHR(ctx->device, vkwsi_temp(VkSwapchainCreateInfoKHR {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = scaling_mode
            ? vkwsi_temp(VkSwapchainPresentScalingCreateInfoEXT {
                .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_SCALING_CREATE_INFO_EXT,
                .scalingBehavior = scaling_mode,
            })
            : nullptr,
        // NOTE: Deferred allocation improves latency when recreating swapchains and reduces the likelihood
        //       of failing to acquire immediately after a resize.
        //       However, it can result in swapchain images being allocated individually, which may have *some* impact
        //       so we might choose to make this configurable too.
        .flags = VK_SWAPCHAIN_CREATE_DEFERRED_MEMORY_ALLOCATION_BIT_KHR,
        .surface               = swapchain->surface,
        .minImageCount         = min_image_count,
        .imageFormat           = info.format,
        .imageColorSpace       = info.color_space,
        .imageExtent           = extent,
        .imageArrayLayers      = info.image_array_layers,
        .imageUsage            = info.image_usage,
        .imageSharingMode      = info.image_sharing_mode,
        .queueFamilyIndexCount = info.queue_family_count,
        .pQueueFamilyIndices   = info.queue_families,
        .preTransform          = info.pre_transform,
        .compositeAlpha        = info.composite_alpha,
        .presentMode           = info.present_mode,
        .oldSwapchain          = swapchain->swapchain,
    }), ctx->alloc, &new_swapchain);
    VKWSI_CHECK(res);

    // Replace the swapchain

    vkwsi_destroy_vk_swapchain(swapchain);
    swapchain->swapchain = new_swapchain;

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
    swapchain->version++;

    return VK_SUCCESS;
}

VkResult vkwsi_swapchain_resize(vkwsi_swapchain* swapchain, VkExtent2D extent)
{
    // TODO: Should this function (or any) be thread safe?

    if (extent.width != swapchain->last_extent.width || extent.height != swapchain->last_extent.height) {
        swapchain->pending_extent = extent;
        // TODO: Instead of marking this out-of-date, we should check surface capabilities if pending != current
        swapchain->out_of_date = true;
    }

    return VK_SUCCESS;
}

VkResult vkwsi_swapchain_acquire(
    vkwsi_swapchain* const* swapchains, uint32_t swapchain_count,
    VkQueue adapter_queue,
    const VkSemaphoreSubmitInfo* _signals, uint32_t _signal_count)
{
    // NOTE: `adapter_queue` technically must be the same for all acquires. As signal operation ordering
    //       guarantees are relied on for timeline correctness in the (pathological) case that separate
    //       acquire calls complete out of orders

    if (swapchain_count == 0) return VK_SUCCESS;

    auto ctx = swapchains[0]->ctx;
    VkResult res;

#if VKWSI_DEBUG_LINEARIZE
        VkFence debug_fence = ctx->debug_fence;
#else
        VkFence debug_fence = nullptr;
#endif

    // NOTE: We recovery acquire binary semaphores by polling the main context timeline semaphore
    //       We could also avoid the additional poll by recovering binary semaphores via the appropriate
    //       `vkwsi_on_swapchain_present_complete`, however this would force worst-case semaphore reuse.
    vkwsi_recover_binary_semaphores(ctx);

    std::vector<VkSemaphoreSubmitInfo> wait_infos(swapchain_count);
    for (uint32_t i = 0; i < swapchain_count; ++i) {
        auto swapchain = swapchains[i];

        // TODO: How do we recover from errors that occur after we have successfully acquired from *some* swapchains
        //       We need to ensure all swapchains are still in a recoverable state. (Wait and release swapchain images?)

        // TODO: In the case that `last_extent` does not match `desired_extent`, but the swapchain is still renderable
        //       and not marked out-of-date, we should still recheck the surface capabilities to see if we can resize
        //       the swapchain to a more desired size.

        // if (!swapchain->swapchain || swapchain->out_of_date) vkwsi_swapchain_recreate(swapchain);

        uint32_t image_idx;

        VkSemaphore wait_semaphore = nullptr;
        res = vkwsi_get_binary_semaphore(ctx, &wait_semaphore);
        VKWSI_CHECK(res);
        res = ctx->SetDebugUtilsObjectNameEXT(ctx->device, vkwsi_temp(VkDebugUtilsObjectNameInfoEXT {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
            .objectType = VK_OBJECT_TYPE_SEMAPHORE,
            .objectHandle = uint64_t(wait_semaphore),
            .pObjectName = "acquire-semaphore",
        }));
        VKWSI_CHECK(res);

        wait_infos[i] = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = wait_semaphore,
        };

        // TODO: Should this have a retry limit? This will only retry on OUT-OF-DATE errors, and those could be returned
        //       an arbitrary number of times up until the user stops a resize operation.

        for (;;) {
            if (swapchain->out_of_date) {
                res = vkwsi_swapchain_recreate(swapchain);
                VKWSI_CHECK(res);
                if (swapchain->out_of_date) {
                    vkwsi_log("WARN: Failed to recreate swapchain due to surface capabilities race, retrying...");
                }
            }

            res = ctx->AcquireNextImageKHR(ctx->device, swapchain->swapchain, UINT64_MAX, wait_semaphore, debug_fence, &image_idx);
            if (res == VK_ERROR_OUT_OF_DATE_KHR) {
                swapchain->out_of_date = true;
                vkwsi_log("WARN: Failed to acquire image due to OUT-OF-DATE condition, retrying...");
                continue;
            }

            break;
        }

        if (res != VK_SUBOPTIMAL_KHR) {
            VKWSI_CHECK(res);
        }
#if VKWSI_DEBUG_LINEARIZE
        vkwsi_log("#### vkwsi - waiting for acquire debug fence to complete");
        res = vkwsi_h_wait_and_reset_fence(ctx, debug_fence);
        vkwsi_log("#### vkwsi - done");
        VKWSI_CHECK(res);
#endif
        swapchain->image_index = image_idx;

        // NOTE: In theory we should not have to wait at this point. As acquiring an
        //       index should imply that all resources from that present are free.
        //       However, without this wait. The validation layers occasionally
        //       complain about vkResetFences being used on a VkFecen that is still
        //       in use. It's possible this is just a VVL false positive, but we work
        //       around it anyway. Ideally we could just:
        //
        //       vkwsi_on_swapchain_present_complete(swapchain, image_idx)
        vkwsi_log("#### vkwsi - waiting for previous presentation to complete");
        res = vkwsi_wait_for_present_complete(swapchain, image_idx);
        vkwsi_log("#### vkwsi - done");
        VKWSI_CHECK(res);

        if (!swapchain->resources[image_idx].view) {
            // NOTE: We create image views lazily, this lets us use deferred swapchain allocation
            //       without any further changes.
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

    // {
    //     for (uint32_t i = 0; i < swapchain_count; ++i) {
    //         // NOTE: We inject out own timeline semaphore to know when we can recover the allocated binary semaphores
    //         auto timeline_value = ++ctx->timeline_value;

    //         {
    //             res = ctx->QueueSubmit2(adapter_queue, 1, vkwsi_temp(VkSubmitInfo2 {
    //                 .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
    //                 .waitSemaphoreInfoCount = 1,
    //                 .pWaitSemaphoreInfos = &wait_infos[i],
    //                 .signalSemaphoreInfoCount = 1,
    //                 .pSignalSemaphoreInfos = vkwsi_temp(VkSemaphoreSubmitInfo {
    //                     .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
    //                     .semaphore = ctx->timeline,
    //                     .value = timeline_value,
    //                 }),
    //             }), debug_fence);
    //             VKWSI_CHECK(res);

    //     #if VKWSI_DEBUG_LINEARIZE
    //             vkwsi_log("#### vkwsi - waiting for adapter submission[{}] to complete", i);
    //             res = ctx->WaitSemaphores(ctx->device, vkwsi_temp(VkSemaphoreWaitInfo {
    //                 .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
    //                 .semaphoreCount = 1,
    //                 .pSemaphores = &ctx->timeline,
    //                 .pValues = &timeline_value,
    //             }), UINT64_MAX);
    //             vkwsi_log("#### vkwsi - done");
    //             VKWSI_CHECK(res);
    //             res = vkwsi_h_wait_and_reset_fence(ctx, debug_fence);
    //             VKWSI_CHECK(res);
    //     #endif
    //         }
    //     }

    // }

    // NOTE: We inject out own timeline semaphore to know when we can recover the allocated binary semaphores
    auto timeline_value = ++ctx->timeline_value;
    std::vector<VkSemaphoreSubmitInfo> signals;
    signals.reserve(_signal_count + 1);
    for (uint32_t i = 0; i < _signal_count; ++i) {
        signals.emplace_back(_signals[i]);
    }
    signals.emplace_back(VkSemaphoreSubmitInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = ctx->timeline,
        .value = timeline_value,
    });

    {
        res = ctx->QueueSubmit2(adapter_queue, 1, vkwsi_temp(VkSubmitInfo2 {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .waitSemaphoreInfoCount = swapchain_count,
            .pWaitSemaphoreInfos = wait_infos.data(),
            .signalSemaphoreInfoCount = uint32_t(signals.size()),
            .pSignalSemaphoreInfos = signals.data(),
        }), debug_fence);
        VKWSI_CHECK(res);

#if VKWSI_DEBUG_LINEARIZE
        vkwsi_log("#### vkwsi - waiting for adapter submission to complete");
        res = vkwsi_h_wait_and_reset_fence(ctx, debug_fence);
        vkwsi_log("#### vkwsi - done");
        VKWSI_CHECK(res);
#endif

        auto& resources = ctx->acquire_resource_release_queue.emplace_back();
        resources.timeline_value = timeline_value;
        resources.semaphores.resize(swapchain_count);
        for (uint32_t i = 0; i < swapchain_count; ++i) {
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
        .version = swapchain->version,
    };
}

VkResult vkwsi_swapchain_present(
    vkwsi_swapchain* const* swapchains, uint32_t swapchain_count,
    VkQueue queue,
    const VkSemaphoreSubmitInfo* waits, uint32_t wait_count, bool host_wait)
{
    if (swapchain_count == 0) return VK_SUCCESS;

    auto ctx = swapchains[0]->ctx;
    VkResult res;

    VkSemaphore binary_sema = nullptr;

#if VKWSI_DEBUG_LINEARIZE
        VkFence debug_fence = ctx->debug_fence;
#else
        VkFence debug_fence = nullptr;
#endif

    if (wait_count > 0) {
        if (host_wait) {
            std::vector<VkSemaphore> semaphores;
            std::vector<uint64_t> values;
            for (uint32_t i = 0; i < wait_count; ++i) {
                semaphores[i] = waits[i].semaphore;
                values[i] = waits[i].value;
            }

            res = ctx->WaitSemaphores(ctx->device, vkwsi_temp(VkSemaphoreWaitInfo {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
                .semaphoreCount = uint32_t(wait_count),
                .pSemaphores = semaphores.data(),
                .pValues = values.data(),
            }), UINT64_MAX);
        } else {
            res = vkwsi_get_binary_semaphore(ctx, &binary_sema);
            VKWSI_CHECK(res);
            res = ctx->SetDebugUtilsObjectNameEXT(ctx->device, vkwsi_temp(VkDebugUtilsObjectNameInfoEXT {
                .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                .objectType = VK_OBJECT_TYPE_SEMAPHORE,
                .objectHandle = uint64_t(binary_sema),
                .pObjectName = "present-semaphore",
            }));
            VKWSI_CHECK(res);

            res = ctx->QueueSubmit2(queue, 1, vkwsi_temp(VkSubmitInfo2 {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                .waitSemaphoreInfoCount = wait_count,
                .pWaitSemaphoreInfos = waits,
                .signalSemaphoreInfoCount = 1,
                .pSignalSemaphoreInfos = vkwsi_temp(VkSemaphoreSubmitInfo {
                    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                    .semaphore = binary_sema,
                }),
            }), debug_fence);
            VKWSI_CHECK(res);

#if VKWSI_DEBUG_LINEARIZE
            res = vkwsi_h_wait_and_reset_fence(ctx, debug_fence);
            VKWSI_CHECK(res);
#endif
        }
    }

    std::vector<VkSwapchainKHR> vk_swapchains(swapchain_count);
    std::vector<uint32_t>             indices(swapchain_count);
    std::vector<VkFence>       present_fences(swapchain_count);
    std::vector<VkResult>             results(swapchain_count);
    for (uint32_t i = 0; i < swapchain_count; ++i) {
        auto& sc = *swapchains[i];
        vk_swapchains[i] = sc.swapchain;
        indices[i] = sc.image_index;

        // TODO: This should probably just be an assert. (We should also add more asserts *everywhere*)
        if (sc.resources[sc.image_index].present_signal_fence) {
            vkwsi_log("ERROR: Unexpected unreturned fence at index {}", sc.image_index);
        }

        VkFence fence;
        res = vkwsi_get_fence(ctx, &fence);
        present_fences[i] = fence;
        sc.resources[sc.image_index].present_signal_fence = fence;
        VKWSI_CHECK(res);
    }

    // NOTE: this is not VKWSI_CHECK'd directly. We check each VkResult in `pResults`
    ctx->QueuePresentKHR(queue, vkwsi_temp(VkPresentInfoKHR {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = vkwsi_temp(VkSwapchainPresentFenceInfoKHR {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR,
            .swapchainCount = swapchain_count,
            .pFences = present_fences.data(),
        }),
        .waitSemaphoreCount = binary_sema ? 1u : 0u,
        .pWaitSemaphores = &binary_sema,
        .swapchainCount = swapchain_count,
        .pSwapchains = vk_swapchains.data(),
        .pImageIndices = indices.data(),
        .pResults = results.data(),
    }));

    for (uint32_t i = 0; i < swapchain_count; ++i) {
        auto& sc = *swapchains[i];
        if (results[i] == VK_ERROR_OUT_OF_DATE_KHR) {
            vkwsi_log("WARN: Present returned OUT-OF-DATE, marking swapchain...");
            sc.out_of_date = true;
            continue;
        }
        if (results[i] != VK_SUBOPTIMAL_KHR) {
            // TODO: Same as acquire, we need to handle a critical error here while leaving everything
            //       in an otherwise recoverable state.
            //       E.g. Note errors, continue on to setup binary semaphore recovery. Then return error code.
            VKWSI_CHECK(results[i]);
        }
    }

    if (binary_sema) {
        // TODO: Presents that fail with VK_ERROR_OUT_OF_DATE_KHR still enqueue their wait operations, thus we need
        //       to consider them before safely releasing the fences and semaphores.
        ctx->present_semaphore_release_map[binary_sema] = swapchain_count;
        for (uint32_t i = 0; i < swapchain_count; ++i) {
            auto* swapchain = swapchains[i];
            swapchain->resources[swapchain->image_index].last_present_wait_semaphore = binary_sema;
        }
    }

#if VKWSI_DEBUG_LINEARIZE
    for (uint32_t i = 0; i < swapchain_count; ++i) {
        res = vkwsi_wait_for_present_complete(swapchains[i], swapchains[i]->image_index);
        VKWSI_CHECK(res);
    }
#endif

    return VK_SUCCESS;
}
