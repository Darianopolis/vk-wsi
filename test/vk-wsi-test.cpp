#include "vk-wsi.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <format>
#include <iostream>
#include <source_location>
#include <chrono>
#include <thread>
#include <mutex>

using namespace std::literals;

// -----------------------------------------------------------------------------

// Force host waits after acquire and rendering work
#define VKWSI_TEST_FORCE_LINEARIZATION 0

#define VKWSI_TEST_REPORT_METRICS 1

// -----------------------------------------------------------------------------

// NOTE: This is because of GPU drivers and Vulkan Validation Layers resulting
//       in many unavoidable leak warnings
// TODO: Leave this to runtime environment configuration?
// TODO: Use a tracking allocator to track *our* leaks?
extern "C" const char* __asan_default_options() { return "detect_leaks=0"; }

// -----------------------------------------------------------------------------

// TODO: Create a separate test harness to keep boilerplate clutter away from test/example logic

template<typename Fn>
struct Defer
{
    Fn fn;

    Defer(Fn&& fn): fn(std::move(fn)) {}
    ~Defer() { fn(); };
};

#define VKWSI_TEST_CONCAT_INTERNAL(a, b) a##b
#define VKWSI_TEST_CONCAT(a, b) VKWSI_TEST_CONCAT_INTERNAL(a, b)
#define VKWSI_TEST_UNIQUE_VAR() VKWSI_TEST_CONCAT(vkwsi_var_, __COUNTER__)

#define defer Defer VKWSI_TEST_UNIQUE_VAR() = [&]

template<typename ...Args>
void log(std::format_string<Args...> fmt, Args&&... args)
{
    std::cout << std::vformat(fmt.get(), std::make_format_args(args...)) << '\n';
}

template<typename... Args>
[[noreturn]] void error(std::source_location loc, std::format_string<Args...> fmt, Args&& ...args)
{
    std::cout << std::format("ERROR({} @ {}):\n", loc.line(), loc.file_name());
    std::cout << std::vformat(fmt.get(), std::make_format_args(args...)) << '\n';
    std::cin.get();
    std::exit(1);
}

struct LocatedVkResult
{
    VkResult res;
    std::source_location loc;

    LocatedVkResult(VkResult _res, std::source_location _loc = std::source_location::current())
        : res(_res), loc(_loc)
    {}
};

VkResult vk_check(LocatedVkResult located_res, auto... allowed)
{
    auto[res, loc] = located_res;
    if (res == VK_SUCCESS || (... || (res == allowed))) return res;
    error(loc, "VkResult = {}", int(res));
}

template<typename Container, typename Fn, typename... Args>
void vk_enumerate(Container& container, Fn&& fn, Args&&... args)
{
    uint32_t count = static_cast<uint32_t>(container.size());
    for (;;) {
        uint32_t old_count = count;
        if constexpr (std::same_as<VkResult, decltype(fn(args..., &count, nullptr))>) {
            vk_check(fn(args..., &count, container.data()), VK_INCOMPLETE);
        } else {
            fn(args..., &count, container.data());
        }

        container.resize(count);
        if (count <= old_count) return;
    }
}

auto ptr_to(auto&& v) { return &v; }

#define vk_instance_fn(fn) PFN_##fn fn = reinterpret_cast<PFN_##fn>(vkGetInstanceProcAddr(instance, #fn)); \
    if (!fn) error(std::source_location::current(), "Instance function " #fn " failed to load!")

#define vk_device_fn(fn) PFN_##fn fn = reinterpret_cast<PFN_##fn>(vkGetDeviceProcAddr(device, #fn)); \
    if (!fn) error(std::source_location::current(), "Device function " #fn " failed to load!")

// -----------------------------------------------------------------------------

int main()
{
    // Options

    static constexpr uint32_t num_windows = 1;
    static constexpr uint32_t frames_in_flight = 3;

// -----------------------------------------------------------------------------

    SDL_Init(SDL_INIT_VIDEO);
    defer {
        SDL_Quit();
    };
    SDL_Vulkan_LoadLibrary(nullptr);
    auto vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());

    log("vkGetInstanceProcAddr: {}", (void*)vkGetInstanceProcAddr);
    if (!vkGetInstanceProcAddr) {
        error(std::source_location::current(), "Could not load vulkan loader");
    }

    std::vector<const char*> instance_extensions {
        VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME,
        VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME,
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
    };
    {
        Uint32 instance_extension_count;
        auto* list = SDL_Vulkan_GetInstanceExtensions(&instance_extension_count);
        for (uint32_t i = 0; i < instance_extension_count; ++i) {
            instance_extensions.emplace_back(list[i]);
        }
    }

    VkInstance instance = {};
    vk_instance_fn(vkCreateInstance);

    log("vkCreateInstance: {}", (void*)vkCreateInstance);

    vk_check(vkCreateInstance(ptr_to(VkInstanceCreateInfo {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = ptr_to(VkApplicationInfo {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .apiVersion = VK_API_VERSION_1_3,
        }),
        .enabledExtensionCount = uint32_t(instance_extensions.size()),
        .ppEnabledExtensionNames = instance_extensions.data(),
    }), nullptr, &instance));

    log("instance: {}", (void*)instance);

    vk_instance_fn(vkEnumeratePhysicalDevices);
    vk_instance_fn(vkGetPhysicalDeviceProperties2);
    vk_instance_fn(vkGetPhysicalDeviceQueueFamilyProperties);
    vk_instance_fn(vkCreateDevice);
    vk_instance_fn(vkGetDeviceProcAddr);
    vk_instance_fn(vkGetPhysicalDeviceSurfaceFormatsKHR);
    vk_instance_fn(vkDestroySurfaceKHR);
    vk_instance_fn(vkDestroyDevice);
    vk_instance_fn(vkDestroyInstance);

    defer { vkDestroyInstance(instance, nullptr); };

    std::vector<VkPhysicalDevice> physical_devices;
    vk_enumerate(physical_devices, vkEnumeratePhysicalDevices, instance);
    for (uint32_t i = 0; i < physical_devices.size(); ++i) {
        VkPhysicalDeviceProperties2 props { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        vkGetPhysicalDeviceProperties2(physical_devices[i], &props);

        std::cout << std::format(" device[{}] = {}\n", i, props.properties.deviceName);
    }

    std::cout << std::format("Number of GPUs: {}\n", physical_devices.size());

    auto physical_device = physical_devices[0];
    {
        VkPhysicalDeviceProperties2 props { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        vkGetPhysicalDeviceProperties2(physical_device, &props);

        std::cout << std::format(" running on: {}\n", props.properties.deviceName);
    }

    // Find graphics queue

    std::vector<VkQueueFamilyProperties> queue_props;
    vk_enumerate(queue_props, vkGetPhysicalDeviceQueueFamilyProperties, physical_device);

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

    vk_check(vkCreateDevice(physical_device, ptr_to(VkDeviceCreateInfo {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = ptr_to(VkPhysicalDeviceVulkan12Features {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
            .pNext = ptr_to(VkPhysicalDeviceVulkan13Features {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
                    .pNext = ptr_to(VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT {
                        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT,
                        .swapchainMaintenance1 = true,
                    }),
                .synchronization2 = true,
            }),
            .timelineSemaphore = true,
        }),
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = ptr_to(VkDeviceQueueCreateInfo {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = queue_family,
            .queueCount = 1,
            .pQueuePriorities = ptr_to(1.f),
        }),
        .enabledExtensionCount = uint32_t(std::size(device_extensions)),
        .ppEnabledExtensionNames = device_extensions,
    }), nullptr, &device));

    defer { vkDestroyDevice(device, nullptr); };

    vk_device_fn(vkGetDeviceQueue);
    vk_device_fn(vkCreateCommandPool);
    vk_device_fn(vkAllocateCommandBuffers);
    vk_device_fn(vkCreateSemaphore);
    vk_device_fn(vkCmdPipelineBarrier2);
    vk_device_fn(vkBeginCommandBuffer);
    vk_device_fn(vkCmdBeginRendering);
    vk_device_fn(vkEndCommandBuffer);
    vk_device_fn(vkQueueSubmit2);
    vk_device_fn(vkWaitSemaphores);
    vk_device_fn(vkDestroyCommandPool);
    vk_device_fn(vkDestroySemaphore);
    vk_device_fn(vkDestroyPipelineLayout);
    vk_device_fn(vkCmdClearColorImage);

    // Get graphics queue

    VkQueue queue = {};
    vkGetDeviceQueue(device, queue_family, 0, &queue);

    // Create timeline semaphore

    VkSemaphore semaphore = {};
    uint64_t semaphore_last_value = {};
    vk_check(vkCreateSemaphore(device, ptr_to(VkSemaphoreCreateInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = ptr_to(VkSemaphoreTypeCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue = 0,
        }),
    }), nullptr, &semaphore));
    defer { vkDestroySemaphore(device, semaphore, nullptr); };
    auto next_sema_value = [&] {
        return ++semaphore_last_value;
    };
    auto wait_semaphore = [&](VkSemaphore sema, uint64_t value) {
        vk_check(vkWaitSemaphores(device, ptr_to(VkSemaphoreWaitInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
            .semaphoreCount = 1,
            .pSemaphores = &sema,
            .pValues = ptr_to(value),
        }), UINT64_MAX));
    };

    // Create command pool and buffers

    VkCommandPool cmd_pool = {};
    vk_check(vkCreateCommandPool(device, ptr_to(VkCommandPoolCreateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queue_family,
    }), nullptr, &cmd_pool));
    defer { vkDestroyCommandPool(device, cmd_pool, nullptr); };

    uint64_t frame = 0;

    struct frame_resources
    {
        VkCommandBuffer cmd = {};
        uint64_t timeline_value = {};
    };

    frame_resources fif_resources[frames_in_flight] = {};

    for (uint32_t i = 0; i < frames_in_flight; ++i) {
        vk_check(vkAllocateCommandBuffers(device, ptr_to(VkCommandBufferAllocateInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = cmd_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        }), &fif_resources[i].cmd));
    }

    // Initialize vk-wsi

    vkwsi_context* context;
    vk_check(vkwsi_context_create(&context, ptr_to(vkwsi_context_info {
        .instance = instance,
        .device = device,
        .physical_device = physical_device,
        .get_instance_proc_addr = vkGetInstanceProcAddr,
    })));
    defer { vkwsi_context_destroy(context); };

    // Create windows and surfaces

    struct window_data
    {
        SDL_Window* window;
        VkSurfaceKHR surface;
        vkwsi_swapchain* swapchain;
        std::atomic<bool> close_requested = false;
        std::atomic<VkExtent2D> extent;
    };

    std::mutex windows_mutex;
    std::vector<std::unique_ptr<window_data>> windows;

    for (uint32_t i = 0; i < num_windows; ++i) {
        auto& wd = *windows.emplace_back(new window_data {});

        // Create window

        wd.window = SDL_CreateWindow(std::format("vk-wsi-{}", i + 1).c_str(), 800, 600, SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);

        if (!SDL_Vulkan_CreateSurface(wd.window, instance, nullptr, &wd.surface)) {
            error(std::source_location::current(), "Failed to create SDL Vulkan surface: {}", SDL_GetError());
        }

        vk_check(vkwsi_swapchain_create(&wd.swapchain, context, wd.surface));

        // Select surface format

        VkSurfaceFormatKHR surface_format = {};
        std::vector<VkSurfaceFormatKHR> surface_formats;
        vk_enumerate(surface_formats, vkGetPhysicalDeviceSurfaceFormatsKHR, physical_device, wd.surface);
        for (auto& f : surface_formats) {
            // if (f.format == VK_FORMAT_R8G8B8A8_SRGB || f.format == VK_FORMAT_B8G8R8A8_SRGB) {
            if (f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) {
                surface_format = f;
                break;
            }
        }

        // Create vk-wsi swapchain

        vkwsi_swapchain_info sw_info = vkwsi_swapchain_info_default();
        sw_info.image_sharing_mode = VK_SHARING_MODE_EXCLUSIVE;
        sw_info.queue_families = &queue_family;
        sw_info.queue_family_count = 1;
        sw_info.image_usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        VkPresentModeKHR present_modes[] { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_KHR };
        sw_info.present_mode = vkwsi_context_pick_present_mode(context, wd.surface, present_modes, std::size(present_modes));

        switch (sw_info.present_mode) {
            break;case VK_PRESENT_MODE_MAILBOX_KHR: sw_info.min_image_count = 4;
            break;case VK_PRESENT_MODE_FIFO_KHR:    sw_info.min_image_count = 2;
            break;default:                          ;
        }

        sw_info.format = surface_format.format;
        sw_info.color_space = surface_format.colorSpace;
        vkwsi_swapchain_set_info(wd.swapchain, &sw_info);

        // Update initial extent

        {
            int w, h;
            SDL_GetWindowSizeInPixels(wd.window, &w, &h);
            VkExtent2D extent { uint32_t(w), uint32_t(h) };
            wd.extent = extent;

            vk_check(vkwsi_swapchain_resize(wd.swapchain, extent));
        }
    }

    defer { wait_semaphore(semaphore, semaphore_last_value); };

#if VKWSI_TEST_REPORT_METRICS
    auto last_report = std::chrono::steady_clock::now();
    uint32_t fps = 0;
#endif

    auto render = [&]() -> bool {
        auto fif = (frame++) % frames_in_flight;
        auto& frame = fif_resources[fif];
        auto cmd = frame.cmd;

        // Wait for previous frame in flight to complete
        // This is only used to guard the command buffer in practice
        // We could also simply allocate a transient command buffer each frame

        wait_semaphore(semaphore, frame.timeline_value);

#if VKWSI_TEST_REPORT_METRICS
        {
            fps++;
            auto now = std::chrono::steady_clock::now();
            if (now - last_report > 1s) {
                log("FPS: {}", fps);
                fps = 0;
                last_report = now;
            }
        }
#endif

        // Update window swapchain sizes
        //
        // NOTE: We do not need to lock `windows_mutex` when iterating over windows if
        //       we do not invalidate iterators or mutate any state that the main thread needs to read.

        for (auto& wd : windows) {
            vk_check(vkwsi_swapchain_resize(wd->swapchain, wd->extent));
        }

        // Handle window destruction

        {
            std::scoped_lock m{ windows_mutex };

            // NOTE: Window destruction is handled across both the Render and Main thread to ensure all resources are destroyed safely.
            //       1. Main thread receieves SDL_EVENT_WINDOW_CLOSE_REQUESTED
            //       2. Main thread marks `close_requested` atomically
            //       3. Render thread sees close requested flag
            //       4. Render thread destroys all Vulkan resources associated with window
            //          (which naturally waits for all prior presentation operations to complete)
            //       5. Render thread registers callback to run on Main thread to finally close the SDL window
            std::erase_if(windows, [&](auto& wd) {
                if (wd->close_requested) {
                    log("Window {} close acknowledge on render thread, destroying Vulkan resources", (void*)wd->window);
                    vkwsi_swapchain_destroy(wd->swapchain);
                    vkDestroySurfaceKHR(instance, wd->surface, nullptr);
                    SDL_RunOnMainThread([](void* window) {
                        log("Window {} resource destruction acknowledged by main thread, destroying SDL window", window);
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

        std::vector<vkwsi_swapchain*> swapchains;
        for (auto& wd : windows) swapchains.emplace_back(wd->swapchain);
        vk_check(vkwsi_swapchain_acquire(swapchains.data(), swapchains.size(), queue, &image_ready, 1));

#if VKWSI_TEST_FORCE_LINEARIZATION
        wait_semaphore(semaphore, image_ready.value);
#endif

        // Record commands

        vk_check(vkBeginCommandBuffer(cmd, ptr_to(VkCommandBufferBeginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        })));

        for (auto& wd : windows) {
            auto current = vkwsi_swapchain_get_current(wd->swapchain);
            auto image = current.image;

            auto transition = [&](VkCommandBuffer cmd, VkImage image,
                VkPipelineStageFlags2 src, VkPipelineStageFlags2 dst,
                VkAccessFlags2 src_access, VkAccessFlags2 dst_access,
                VkImageLayout old_layout, VkImageLayout new_layout)
            {
                vkCmdPipelineBarrier2(cmd, ptr_to(VkDependencyInfo {
                    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                    .imageMemoryBarrierCount = 1,
                    .pImageMemoryBarriers = ptr_to(VkImageMemoryBarrier2 {
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

            // TODO: Update to latest git version of VVL and aim for zero synchronization validation warnings
            //       Need to check on status of synchronization layers when using timeline semaphores!

            // transition(cmd, image,
            //     VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            //     0, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            //     VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            transition(cmd, image,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_WRITE_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            vkCmdClearColorImage(cmd, image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                ptr_to(VkClearColorValue{.float32{0.2f, 0.2f, 0.2f, 1.f}}),
                1, ptr_to(VkImageSubresourceRange { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }));

            // transition(cmd, image,
            //     VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR, VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
            //     VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            //     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

            transition(cmd, image,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_WRITE_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        }

        vk_check(vkEndCommandBuffer(cmd));

        // Submit commands

        VkSemaphoreSubmitInfoKHR render_complete {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = semaphore,
            .value = next_sema_value(),
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        };

        vk_check(vkQueueSubmit2(queue, 1, ptr_to(VkSubmitInfo2 {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .waitSemaphoreInfoCount = 1,
            .pWaitSemaphoreInfos = &image_ready,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = ptr_to(VkCommandBufferSubmitInfo {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                .commandBuffer = cmd,
            }),
            .signalSemaphoreInfoCount = 1,
            .pSignalSemaphoreInfos = &render_complete,
        }), nullptr));

#if VKWSI_TEST_FORCE_LINEARIZATION
        wait_semaphore(semaphore, render_complete.value);
#endif

        frame.timeline_value = render_complete.value;

        // Present to swapchains

        vkwsi_swapchain_present(swapchains.data(), swapchains.size(), queue, &render_complete, 1, false);

        return true;
    };

    // Launch Render thread

    defer { log("Render thread closed"); };

    std::jthread render_thread {[&] {
        while (render())
            ;
    }};

    // NOTE: Window size events need to be handled from an event watched as the main event loop
    //       isn't running during resize operations on Windows due to modal resizing.

    auto sdl_event_listener = [&](SDL_Event* event) {
        if (SDL_IsMainThread()) {
            if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                auto w = event->window.data1;
                auto h = event->window.data2;
                std::scoped_lock m{ windows_mutex };
                for (auto& wd : windows) {
                    if (SDL_GetWindowID(wd->window) == event->window.windowID) {
                        // log("Window {} resized ({}, {})", (void*)wd->window, w, h);
                        wd->extent = { uint32_t(w), uint32_t(h) };
                        break;
                    }
                }
            }
        } else {
            log("WARN: Event Listener called from other thread!");
        }
    };
    SDL_AddEventWatch([](void *userdata, SDL_Event *event) -> bool {
        (*(decltype(sdl_event_listener)*)userdata)(event);
        return true;
    }, &sdl_event_listener);

    // Main thread event loop

    SDL_Event event;
    while (SDL_WaitEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            break;
        }

        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            std::scoped_lock m{ windows_mutex };
            for (auto& wd : windows) {
                auto id = SDL_GetWindowID(wd->window);
                if (id == event.window.windowID) {
                    log("Window {} close requested (id = {})", (void*)wd->window, id);
                    wd->close_requested = true;
                    break;
                }
            }
        }
    }

    // Shutting down

    {
        // Mark all remaining windows to be closed by render thread
        // The render thread will end when all windows are closed

        // NOTE: At this point the SDL_RunInMainThread callbacks for any remaining `SDL_Window`s
        //       won't run, as the event loop has already shut down. But this is ok as SDL
        //       will close any remaining window resources when the VIDEO subsystem is shut down.

        std::scoped_lock m{ windows_mutex };
        for (auto& wd : windows) {
            if (SDL_GetWindowID(wd->window) == event.window.windowID) {
                wd->close_requested = true;
                break;
            }
        }
    }

    log("Quit event receieved, waiting for render loop to complete");
}
