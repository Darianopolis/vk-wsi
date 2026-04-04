#include "vkwsi.hpp"
#include "vkwsi-util.hpp"

#include <cstring>
#include <algorithm>

static
bool infos_are_equal(const vkwsi_acquire_info& a, const vkwsi_acquire_info& b)
{
    static_assert(std::has_unique_object_representations_v<vkwsi_acquire_info>);
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

static
bool needs_recreate(vkwsi_swapchain* swapchain, const vkwsi_acquire_info* info)
{
    if (swapchain->out_of_date || !infos_are_equal(swapchain->info, *info)) {
        swapchain->info = *info;
        return true;
    }

    return false;
}

void vkwsi_swapchain_destroy_resources(vkwsi_swapchain* swapchain)
{
    auto* ctx = swapchain->ctx;

    for (auto& image : swapchain->images) {
        ctx->vk.DestroyImageView(ctx->device, image.view, ctx->alloc);
    }
    ctx->vk.DestroySwapchainKHR(ctx->device, swapchain->swapchain, ctx->alloc);
}

static
VkResult recreate_swapchain(vkwsi_swapchain* swapchain)
{
    auto* ctx = swapchain->ctx;

    auto& info = swapchain->info;

    VkSurfacePresentScalingCapabilitiesEXT scaling_caps = {
        .sType = VK_STRUCTURE_TYPE_SURFACE_PRESENT_SCALING_CAPABILITIES_EXT,
    };
    VkSurfaceCapabilities2KHR caps = {
        .sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR,
        .pNext = &scaling_caps,
    };

    VKWSI_CHECK(ctx->vk.GetPhysicalDeviceSurfaceCapabilities2KHR(ctx->physical_device, vkwsi_ptr_to(VkPhysicalDeviceSurfaceInfo2KHR {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR,
        .pNext = vkwsi_ptr_to(VkSurfacePresentModeKHR {
            .sType = VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_KHR,
            .presentMode = swapchain->info.present_mode,
        }),
        .surface = swapchain->surface,
    }), &caps));

    auto& surface_caps = caps.surfaceCapabilities;

    auto extent = VkExtent2D {
        .width = std::clamp(info.extent.width, surface_caps.minImageExtent.width, surface_caps.maxImageExtent.width),
        .height = std::clamp(info.extent.height, surface_caps.minImageExtent.height, surface_caps.maxImageExtent.height),
    };

    VkPresentScalingFlagsKHR scaling_mode = {};
    if (scaling_caps.supportedPresentScaling) {
        auto min = scaling_caps.minScaledImageExtent;
        auto max = scaling_caps.maxScaledImageExtent;

        VkExtent2D scaled = {std::clamp(info.extent.width, min.width, max.width), std::clamp(info.extent.height, min.height, max.height)};
        if (scaled.width == info.extent.width && scaled.height == info.extent.height) {

            if (scaling_caps.supportedPresentScaling & VK_PRESENT_SCALING_ONE_TO_ONE_BIT_EXT) {
                scaling_mode = VK_PRESENT_SCALING_ONE_TO_ONE_BIT_EXT;
            } else if (scaling_caps.supportedPresentScaling & VK_PRESENT_SCALING_ASPECT_RATIO_STRETCH_BIT_EXT) {
                scaling_mode = VK_PRESENT_SCALING_ASPECT_RATIO_STRETCH_BIT_EXT;
            } else if (scaling_caps.supportedPresentScaling & VK_PRESENT_SCALING_STRETCH_BIT_EXT) {
                scaling_mode = VK_PRESENT_SCALING_STRETCH_BIT_EXT;
            } else if (scaling_caps.supportedPresentScaling) {
                // Fallback to selecting the "first" available scaling mode if we don't recognize any
                scaling_mode = VkPresentScalingFlagBitsEXT(1 << std::countr_zero(scaling_caps.supportedPresentScaling));
            }

            if (scaling_mode) {
                extent = scaled;
            }
        }

        VkPresentScalingFlagsEXT flags;
    }

    info.extent = extent;

    auto min_image_count = surface_caps.minImageCount;
    if (surface_caps.maxImageCount) min_image_count = std::min(min_image_count, surface_caps.maxImageCount);

    VkSwapchainKHR new_swapchain;
    VKWSI_CHECK(ctx->vk.CreateSwapchainKHR(ctx->device, vkwsi_ptr_to(VkSwapchainCreateInfoKHR {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = scaling_mode
            ? vkwsi_ptr_to(VkSwapchainPresentScalingCreateInfoEXT {
                .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_SCALING_CREATE_INFO_EXT,
                .scalingBehavior = scaling_mode,
            })
            : nullptr,
        // NOTE: Deferred allocation improves latency when recreating swapchains and reduces the likelihood
        //       of failing to acquire immediately after a resize.
        //       However, it can result in swapchain images being allocated individually, which may have *some* impact
        //       so we might choose to make this configurable too.
        .flags = VK_SWAPCHAIN_CREATE_DEFERRED_MEMORY_ALLOCATION_BIT_KHR,
        .surface          = swapchain->surface,
        .minImageCount    = min_image_count,
        .imageFormat      = info.format,
        .imageColorSpace  = info.color_space,
        .imageExtent      = extent,
        .imageArrayLayers = info.image_array_layers,
        .imageUsage       = info.image_usage,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform     = info.pre_transform,
        .compositeAlpha   = info.composite_alpha,
        .presentMode      = info.present_mode,
        .oldSwapchain     = swapchain->swapchain,
    }), ctx->alloc, &new_swapchain));

    std::vector<VkImage> images;
    VKWSI_CHECK(vkwsi_enumerate(images, ctx->vk.GetSwapchainImagesKHR, ctx->device, new_swapchain));

    vkwsi_swapchain_destroy_resources(swapchain);

    swapchain->out_of_date = false;
    swapchain->swapchain = new_swapchain;

    swapchain->images.resize(images.size());
    for (uint32_t i = 0; i < images.size(); ++i) {
        swapchain->images[i] = vkwsi_image {
            .swapchain = swapchain,
            .image = images[i],
            .view = nullptr,
            .index = i,
            .extent = extent,
        };
    }

    return VK_SUCCESS;
}

vkwsi_acquire_info vkwsi_acquire_info_default()
{
    return {
        .format = {},
        .color_space = {},

        .image_array_layers = 1,
        .image_usage = {},

        .pre_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,

        .present_mode = VK_PRESENT_MODE_FIFO_KHR,
    };
}

VkResult vkwsi_acquire(vkwsi_swapchain* swapchain, const vkwsi_acquire_info* info, vkwsi_image** out_image)
{
    auto* ctx = swapchain->ctx;

    VkFence fence = nullptr;
    VKWSI_CHECK(ctx->vk.CreateFence(ctx->device, vkwsi_ptr_to(VkFenceCreateInfo {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    }), ctx->alloc, &fence));
    VKWSI_DEFER { ctx->vk.DestroyFence(ctx->device, fence, ctx->alloc); };

    for (;;) {
        if (needs_recreate(swapchain, info)) {
            VKWSI_CHECK(recreate_swapchain(swapchain));
        }

        uint32_t image_index;
        VkResult acquire_result = ctx->vk.AcquireNextImageKHR(ctx->device, swapchain->swapchain, UINT64_MAX, nullptr, fence, &image_index);
        if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) {
            continue;
        }
        VKWSI_CHECK(acquire_result);

        VKWSI_CHECK(ctx->vk.WaitForFences(ctx->device, 1, &fence, true, UINT64_MAX));

        auto* image = &swapchain->images[image_index];
        if (!image->view) {
            VKWSI_CHECK(ctx->vk.CreateImageView(ctx->device, vkwsi_ptr_to(VkImageViewCreateInfo {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = image->image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = swapchain->info.format,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
            }), ctx->alloc, &image->view));
        }

        *out_image = image;

        return VK_SUCCESS;
    }
}

VkExtent2D vkwsi_image_get_extent(vkwsi_image* image)
{
    return image->extent;
}

VkImage vkwsi_image_get_image(vkwsi_image* image)
{
    return image->image;
}

VkImageView vkwsi_image_get_view(vkwsi_image* image)
{
    return image->view;
}
