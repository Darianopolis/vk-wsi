# Vulkan WSI Library

This library aims to wrap the Vulkan WSI interface in a slightly more friendly API:

- Allow clients to communicate entirely in timeline semaphores. No special casing sync for WSI.
- Adapt the legacy binary semaphore interface onto timeline sempahores.
- Utilize present fences for *correct* swapchain resource lifetime management without relying on queue wait idles.
- Use a minimal number of semaphores and fences to achieve highest throughput and minimal latency on presentation.
- Avoid requiring any explicit concept of a "frame".

### Non-Goals

- Support every possible combination of supported Vulkan extensions.
    - Too much core functionality relies on the presence of a handful extensions.
    - No extensions here have any special hardware requirements.
- 100% API coverage of the underlying WSI interface.

# Usage

#### Create context

```c++
vkwsi_context* vkwsi;
vkwsi_context_info info = {
    .instance = instance,
    .device = device,
    .physical_device = physical_device,
    .get_instance_proc_addr = vkGetInstanceProcAddr,
};
VkResult res = vkwsi_context_create(&vkwsi, &info);
```

#### Create swapchain

```c++
vkwsi_swapchain* swapchain;
VkResult res = vkwsi_swapchain_create(&swapchain, vkwsi, surface);
vkwsi_swapchain_info info = vkwsi_swapchain_info_default();
info.image_usage = ...;
vkwsi_swapchain_set_info(swapchain, &info);
```

#### Resize and acquire

```c++
vkwsi_swapchain_resize(swapchain, extent);

VkSemaphoreSubmitInfoKHR image_ready = { ... };
VkResult res = vkwsi_swapchain_acquire(swapchain, 1, queue, &image_ready, 1);

vkwsi_swapchain_image current = vkwsi_swapchain_get_current(swapchain);
```

#### Present

```c++
VkSemaphoreSubmitInfoKHR render_complete = { ... };
VkResult res = vkwsi_swapchain_present(swapchain, 1, queue, &render_complete, 1, false);
```
## Vulkan Extensions

A handful of extensions are required, there are no optional extensions in the interest of keeping a focus on the core functionality of the library. All these extensions should be widely available on any actively supported hardware, and there are no particular hardware requirements.

#### Instance

- `VK_KHR_get_surface_capabilities2`
- `VK_EXT_surface_maintenance1`

#### Device

- `VK_KHR_swapchain`
- `VK_EXT_swapchain_maintenance1` - `swapchainMaintenance1`
- `VK_KHR_timeline_semaphores` or Vulkan `1.2` - `timelineSemaphore`
- `VK_KHR_synchronization2` or Vulkan `1.3` - `synchronization2`

## Building

The library is available as a simple CMake project. Simply add and link against the `vk-wsi::vk-wsi` target (prefer the alias over the internal underlying `vk-wsi` target).

You will need a C++20 capable compiler to build the library.

Pass/Enable `-DVKWSI_BUILD_TESTS=ON` to build the example program (this will fetch SDL)
