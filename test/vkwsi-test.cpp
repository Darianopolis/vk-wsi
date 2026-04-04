#include "vkwsi.h"

#include "../src/vkwsi-util.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <format>
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <cmath>
#include <stacktrace>
#include <print>

using namespace std::literals;

// -----------------------------------------------------------------------------

template<typename... Args>
[[noreturn]] void error(std::format_string<Args...> fmt, Args&& ...args)
{
    std::cerr << std::format(fmt, std::forward<Args>(args)...) << '\n'
              << std::stacktrace::current() << '\n';
    std::exit(1);
}

// -----------------------------------------------------------------------------

VkResult check(VkResult res, auto... allowed)
{
    if (res == VK_SUCCESS || (... || (res == allowed))) return res;
    error("VkResult = {}", int(res));
}

#define instance_fn(fn) PFN_##fn fn = reinterpret_cast<PFN_##fn>(vkGetInstanceProcAddr(instance, #fn)); \
    if (!fn) error("Instance function " #fn " failed to load!")

#define device_fn(fn) PFN_##fn fn = reinterpret_cast<PFN_##fn>(vkGetDeviceProcAddr(device, #fn)); \
    if (!fn) error("Device function " #fn " failed to load!")

// -----------------------------------------------------------------------------

int main()
{
    // Options

    static constexpr bool report_metrics = true;
    static constexpr uint32_t num_windows = 1;
    static constexpr uint32_t frames_in_flight = 3;
    static constexpr VkExtent2D initial_window_size = { 800, 600 };

    // Initialize SDL

    SDL_Init(SDL_INIT_VIDEO);
    VKWSI_DEFER { SDL_Quit(); };
    SDL_Vulkan_LoadLibrary(nullptr);
    auto vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());

    std::println("Using SDL3 ({})", SDL_GetCurrentVideoDriver());

    if (!vkGetInstanceProcAddr) {
        error("Could not load vulkan loader");
    }

    // Create instance

    std::vector<const char*> instance_extensions {
        VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME,
        VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME,
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
    };
    {
        uint32_t instance_extension_count;
        auto* list = SDL_Vulkan_GetInstanceExtensions(&instance_extension_count);
        for (uint32_t i = 0; i < instance_extension_count; ++i) {
            instance_extensions.emplace_back(list[i]);
        }
    }

    VkInstance instance = {};
    instance_fn(vkCreateInstance);

    check(vkCreateInstance(vkwsi_ptr_to(VkInstanceCreateInfo {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = vkwsi_ptr_to(VkApplicationInfo {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .apiVersion = VK_API_VERSION_1_3,
        }),
        .enabledExtensionCount = uint32_t(instance_extensions.size()),
        .ppEnabledExtensionNames = instance_extensions.data(),
    }), nullptr, &instance));

    instance_fn(vkEnumeratePhysicalDevices);
    instance_fn(vkGetPhysicalDeviceProperties2);
    instance_fn(vkGetPhysicalDeviceQueueFamilyProperties);
    instance_fn(vkCreateDevice);
    instance_fn(vkGetDeviceProcAddr);
    instance_fn(vkGetPhysicalDeviceSurfaceFormatsKHR);
    instance_fn(vkDestroySurfaceKHR);
    instance_fn(vkDestroyDevice);
    instance_fn(vkDestroyInstance);

    VKWSI_DEFER { vkDestroyInstance(instance, nullptr); };

    std::vector<VkPhysicalDevice> physical_devices;
    check(vkwsi_enumerate(physical_devices, vkEnumeratePhysicalDevices, instance));
    std::println("vkEnumeratePhysicalDevices(count = {})", physical_devices.size());
    for (uint32_t i = 0; i < physical_devices.size(); ++i) {
        VkPhysicalDeviceProperties2 props { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        vkGetPhysicalDeviceProperties2(physical_devices[i], &props);

        std::println(" device[{}] = {}", i, props.properties.deviceName);
    }

    auto physical_device = physical_devices[0];
    {
        VkPhysicalDeviceProperties2 props { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        vkGetPhysicalDeviceProperties2(physical_device, &props);

        std::println("Selected: {}", props.properties.deviceName);
    }
    std::println("--------------------------------------------------------------------------------");

    // Find graphics queue

    std::vector<VkQueueFamilyProperties> queue_props;
    vkwsi_enumerate(queue_props, vkGetPhysicalDeviceQueueFamilyProperties, physical_device);

    uint32_t queue_family = ~0u;
    for (uint32_t i = 0; i < queue_props.size(); ++i) {
        if (queue_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queue_family = i;
            break;
        }
    }

    // Create logical device

    VkDevice device = {};
    const char* device_extensions[] {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,
    };

    check(vkCreateDevice(physical_device, vkwsi_ptr_to(VkDeviceCreateInfo {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = vkwsi_ptr_to(VkPhysicalDeviceVulkan12Features {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
            .pNext = vkwsi_ptr_to(VkPhysicalDeviceVulkan13Features {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
                    .pNext = vkwsi_ptr_to(VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT {
                        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT,
                        .swapchainMaintenance1 = true,
                    }),
                .synchronization2 = true,
            }),
            .timelineSemaphore = true,
        }),
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = vkwsi_ptr_to(VkDeviceQueueCreateInfo {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = queue_family,
            .queueCount = 1,
            .pQueuePriorities = vkwsi_ptr_to(1.f),
        }),
        .enabledExtensionCount = uint32_t(std::size(device_extensions)),
        .ppEnabledExtensionNames = device_extensions,
    }), nullptr, &device));

    VKWSI_DEFER { vkDestroyDevice(device, nullptr); };

    device_fn(vkGetDeviceQueue);
    device_fn(vkCreateCommandPool);
    device_fn(vkAllocateCommandBuffers);
    device_fn(vkCreateSemaphore);
    device_fn(vkCmdPipelineBarrier2);
    device_fn(vkBeginCommandBuffer);
    device_fn(vkCmdBeginRendering);
    device_fn(vkEndCommandBuffer);
    device_fn(vkQueueSubmit2);
    device_fn(vkWaitSemaphores);
    device_fn(vkDestroyCommandPool);
    device_fn(vkDestroySemaphore);
    device_fn(vkDestroyPipelineLayout);
    device_fn(vkCmdClearColorImage);

    // Get graphics queue

    VkQueue queue = {};
    vkGetDeviceQueue(device, queue_family, 0, &queue);

    // Create timeline semaphore

    VkSemaphore semaphore = {};
    uint64_t semaphore_last_value = {};
    check(vkCreateSemaphore(device, vkwsi_ptr_to(VkSemaphoreCreateInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = vkwsi_ptr_to(VkSemaphoreTypeCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue = 0,
        }),
    }), nullptr, &semaphore));
    VKWSI_DEFER { vkDestroySemaphore(device, semaphore, nullptr); };
    auto next_sema_value = [&] {
        return ++semaphore_last_value;
    };
    auto wait_semaphore = [&](VkSemaphore sema, uint64_t value) {
        check(vkWaitSemaphores(device, vkwsi_ptr_to(VkSemaphoreWaitInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
            .semaphoreCount = 1,
            .pSemaphores = &sema,
            .pValues = vkwsi_ptr_to(value),
        }), UINT64_MAX));
    };

    // Create command pool and buffers

    VkCommandPool cmd_pool = {};
    check(vkCreateCommandPool(device, vkwsi_ptr_to(VkCommandPoolCreateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queue_family,
    }), nullptr, &cmd_pool));
    VKWSI_DEFER { vkDestroyCommandPool(device, cmd_pool, nullptr); };

    // Animated color value to show render progress

    auto time_start = std::chrono::steady_clock::now();
    auto get_clear_color = [&] {

        static constexpr auto period_ms = 4000;
        static constexpr float amplitude = 0.5f;

        auto now = std::chrono::steady_clock::now();
        auto delta_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - time_start).count();
        auto normalized_ms = (delta_ms - period_ms * (delta_ms / period_ms));
        auto normalized_s = float(normalized_ms) / period_ms;
        auto wave = std::sin(normalized_s * std::numbers::pi_v<float>) * amplitude;
        return VkClearColorValue{.float32{0.3f, 0.3f, wave, 1.f}};
    };

    uint64_t frame = 0;

    struct frame_resources
    {
        VkCommandBuffer cmd = {};
        uint64_t timeline_value = {};
    };

    frame_resources fif_resources[frames_in_flight] = {};

    for (uint32_t i = 0; i < frames_in_flight; ++i) {
        check(vkAllocateCommandBuffers(device, vkwsi_ptr_to(VkCommandBufferAllocateInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = cmd_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        }), &fif_resources[i].cmd));
    }

    // Initialize vkwsi

    vkwsi_context* vkwsi;
    check(vkwsi_context_create(&vkwsi, vkwsi_ptr_to(vkwsi_context_info {
        .instance = instance,
        .device = device,
        .physical_device = physical_device,
        .get_instance_proc_addr = vkGetInstanceProcAddr,
    })));
    VKWSI_DEFER { vkwsi_context_destroy(vkwsi); };

    // Create windows and surfaces

    struct window_data
    {
        SDL_Window* window;
        VkSurfaceKHR surface;
        vkwsi_swapchain* swapchain;
        bool close_requested = false;
        std::atomic<VkExtent2D> extent;
        vkwsi_acquire_info info;
    };

    std::mutex windows_mutex;
    std::vector<std::unique_ptr<window_data>> windows;

    for (uint32_t i = 0; i < num_windows; ++i) {
        auto& wd = *windows.emplace_back(new window_data {});

        // Create window

        auto title = std::format("vkwsi-{}", i + 1);

        wd.window = SDL_CreateWindow(
            title.c_str(),
            initial_window_size.width, initial_window_size.height,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);

        if (!SDL_Vulkan_CreateSurface(wd.window, instance, nullptr, &wd.surface)) {
            error("Failed to create SDL Vulkan surface: {}", SDL_GetError());
        }

        check(vkwsi_swapchain_create(&wd.swapchain, vkwsi, wd.surface));

        // Select surface format

        VkSurfaceFormatKHR surface_format = {};
        std::vector<VkSurfaceFormatKHR> surface_formats;
        check(vkwsi_enumerate(surface_formats, vkGetPhysicalDeviceSurfaceFormatsKHR, physical_device, wd.surface));
        for (auto& f : surface_formats) {
            // if (f.format == VK_FORMAT_R8G8B8A8_SRGB || f.format == VK_FORMAT_B8G8R8A8_SRGB) {
            if (f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) {
                surface_format = f;
                break;
            }
        }

        // Create vkwsi swapchain

        wd.info = vkwsi_acquire_info_default();
        wd.info.image_usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        auto present_modes = std::to_array<VkPresentModeKHR>({
            VK_PRESENT_MODE_MAILBOX_KHR,
            VK_PRESENT_MODE_FIFO_KHR,
        });
        wd.info.present_mode = vkwsi_select_present_mode(vkwsi, wd.surface, present_modes.data(), present_modes.size());

        wd.info.format = surface_format.format;
        wd.info.color_space = surface_format.colorSpace;

        // Update initial extent

        {
            int w, h;
            SDL_GetWindowSizeInPixels(wd.window, &w, &h);
            VkExtent2D extent { uint32_t(w), uint32_t(h) };
            wd.extent = extent;

            std::println("window[{}] initial size ({}, {})", i, w, h);
        }
    }

    VKWSI_DEFER { wait_semaphore(semaphore, semaphore_last_value); };

    struct {
        std::chrono::steady_clock::time_point last_report = std::chrono::steady_clock::now();
        uint32_t fps = 0;
    } stats;

    auto render = [&]() -> bool {
        auto fif = (frame++) % frames_in_flight;
        auto& frame_resource = fif_resources[fif];
        auto cmd = frame_resource.cmd;

        // Wait for previous frame in flight to complete
        // This is only used to guard the command buffer in practice
        // We could also simply allocate a transient command buffer each frame

        wait_semaphore(semaphore, frame_resource.timeline_value);

        if (report_metrics) {
            stats.fps++;
            auto now = std::chrono::steady_clock::now();
            if (now - stats.last_report > 1s) {
                std::println("FPS: {}", stats.fps);
                stats.fps = 0;
                stats.last_report = now;
            }
        }

        // Handle window destruction

        {
            std::unique_lock m{ windows_mutex };

            // NOTE: Window destruction is handled across both the Render and Main thread to ensure all resources are destroyed safely.
            //       1. Main thread receieves SDL_EVENT_WINDOW_CLOSE_REQUESTED
            //       2. Main thread marks `close_requested` atomically
            //       3. Render thread sees close requested flag
            //       4. Render thread destroys all Vulkan resources associated with window
            //          (which naturally waits for all prior presentation operations to complete)
            //       5. Render thread registers callback to run on Main thread to finally close the SDL window
            std::erase_if(windows, [&](auto& wd) {
                if (wd->close_requested) {
                    std::println("Window {} close acknowledge on render thread, destroying Vulkan resources", (void*)wd->window);
                    vkwsi_swapchain_destroy(wd->swapchain);
                    vkDestroySurfaceKHR(instance, wd->surface, nullptr);
                    SDL_RunOnMainThread([](void* window) {
                        std::println("Window {} resource destruction acknowledged by main thread, destroying SDL window", window);
                        SDL_DestroyWindow((SDL_Window*)window);
                    }, wd->window, false);
                    return true;
                }
                return false;
            });

            if (windows.empty()) return false;
        }

        // Acquire swapchain images

        VkSemaphoreSubmitInfoKHR image_ready {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = semaphore,
            .value = next_sema_value(),
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        };

        std::vector<vkwsi_image*> images(windows.size());
        // NOTE: We do not need to lock `windows_mutex` when iterating over windows if
        //       we do not invalidate iterators or attempt to read `close_requested`
        for (uint32_t i = 0; i < windows.size(); ++i) {
            windows[i]->info.extent = windows[i]->extent;
            check(vkwsi_acquire(windows[i]->swapchain, &windows[i]->info, &images[i]));
        }

        check(vkwsi_transfer(vkwsi, images.data(), images.size(), queue, &image_ready, 1));

        // Record commands

        check(vkBeginCommandBuffer(cmd, vkwsi_ptr_to(VkCommandBufferBeginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        })));

        for (uint32_t i = 0; i < windows.size(); ++i) {
            auto& wd = windows[i];
            auto* current = images[i];
            auto image = vkwsi_image_get_image(current);
            auto extent = vkwsi_image_get_extent(current);

            auto transition = [&](VkCommandBuffer cmd, VkImage image,
                VkPipelineStageFlags2 src, VkPipelineStageFlags2 dst,
                VkAccessFlags2 src_access, VkAccessFlags2 dst_access,
                VkImageLayout old_layout, VkImageLayout new_layout)
            {
                vkCmdPipelineBarrier2(cmd, vkwsi_ptr_to(VkDependencyInfo {
                    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                    .imageMemoryBarrierCount = 1,
                    .pImageMemoryBarriers = vkwsi_ptr_to(VkImageMemoryBarrier2 {
                        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                        .srcStageMask = src,
                        .srcAccessMask = src_access,
                        .dstStageMask = dst,
                        .dstAccessMask = dst_access,
                        .oldLayout = old_layout,
                        .newLayout = new_layout,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .image = image,
                        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
                    }),
                }));
            };

            transition(cmd, image,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                0, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            vkCmdClearColorImage(cmd, image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                vkwsi_ptr_to(get_clear_color()),
                1, vkwsi_ptr_to(VkImageSubresourceRange { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }));

            transition(cmd, image,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR, VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
                VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        }

        check(vkEndCommandBuffer(cmd));

        // Submit commands

        VkSemaphoreSubmitInfoKHR render_complete {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = semaphore,
            .value = next_sema_value(),
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        };

        check(vkQueueSubmit2(queue, 1, vkwsi_ptr_to(VkSubmitInfo2 {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .waitSemaphoreInfoCount = 1,
            .pWaitSemaphoreInfos = &image_ready,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = vkwsi_ptr_to(VkCommandBufferSubmitInfo {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                .commandBuffer = cmd,
            }),
            .signalSemaphoreInfoCount = 1,
            .pSignalSemaphoreInfos = &render_complete,
        }), nullptr));

        frame_resource.timeline_value = render_complete.value;

        // Present to swapchains

        check(vkwsi_present(vkwsi, images.data(), images.size(), queue, &render_complete, 1));

        return true;
    };

    // Launch Render thread

    VKWSI_DEFER { std::println("Render thread closed"); };

    std::jthread render_thread {[&] {
        while (render())
            ;
    }};

    // Main thread event loop

    auto on_window_resize = [&](decltype(window_data::window) window, int w, int h) {
        std::scoped_lock m{ windows_mutex };
        for (auto& wd : windows) {
            if (wd->window == window) {
                std::println("Window {} resized ({}, {})", (void*)wd->window, w, h);
                wd->extent = { uint32_t(w), uint32_t(h) };
                break;
            }
        }
    };

    // NOTE: Window size events need to be handled from an event watched as the main event loop
    //       isn't running during resize operations on Windows due to modal resizing.
    auto event_watch = [&](SDL_Event* event) {
        if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            on_window_resize(SDL_GetWindowFromEvent(event), event->window.data1, event->window.data2);
        }
    };
    SDL_AddEventWatch([](void *userdata, SDL_Event *event) -> bool {
        (*(decltype(event_watch)*)userdata)(event);
        return true;
    }, &event_watch);

    SDL_Event event;
    while (SDL_WaitEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            std::println("--------------------------------------------------------------------------------");
            std::println("SDL Quit event receieved, exiting main loop");
            break;
        } else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            std::scoped_lock m{ windows_mutex };
            auto event_window = SDL_GetWindowFromEvent(&event);
            for (auto& wd : windows) {
                if (wd->window == event_window) {
                    std::println("Window {} close requested (id = {})", (void*)wd->window, event.window.windowID);
                    wd->close_requested = true;
                    break;
                }
            }
        }
    }

    {
        // Mark all remaining windows to be closed by render thread
        // The render thread will end when all windows are closed

        // NOTE: At this point the SDL_RunOnMainThread callbacks for any remaining `SDL_Window`s
        //       won't run, as the event loop has already shut down. But this is ok as SDL
        //       will close any remaining window resources when the VIDEO subsystem is shut down.

        std::scoped_lock m{ windows_mutex };
        for (auto& wd : windows) {
            wd->close_requested = true;
        }
    }

    std::println("All windows closed, waiting for render loop to complete");
}
