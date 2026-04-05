# Vulkan WSI Library

This library aims to wrap a subset of the Vulkan WSI interface in a more friendly API:

- Dynamic swapchain recreation
- Timeline semaphore synchronization
- No fixed "frames in flight" required

## Usage

#### Include

```c++
#include <vkwsi.h>
```

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

VkSurfaceKHR surface = ...;
VkResult res = vkwsi_swapchain_create(&swapchain, vkwsi, surface);
```

#### Acquire image(s)

```c++
vkwsi_image* image;

vkwsi_acquire_info info = { ... };
VkResult res = vkwsi_acquire(swapchain, &info, &image);
```

#### Transfer sync from acquired images to a number of timeline semaphores

```c++
VkSemaphoreSubmitInfoKHR image_ready = { ... };
vkwsi_transfer_info transfer = { image, layout };
VkResult res = vkwsi_transfer(vkwsi, &transfer, 1, queue, &image_ready, 1);
```

#### Record

```c++
VkExtent2D extent = vkwsi_image_get_extent(image);
VkImageView view = vkwsi_image_get_image_view(image);
// ...
```

#### Submit

```c++
VkSemaphoreSubmitInfoKHR render_complete = { ... };
VkSubmitInfo2 submit_info = {
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
    .waitSemaphoreInfoCount = 1,
    .pWaitSemaphoreInfos = &image_ready,
    // ...
    .signalSemaphoreInfoCount = 1,
    .pSignalSemaphoreInfos = &render_complete,
}
VkResult res = vkQueueSubmit2(queue, 1, &submit_info, nullptr);
```

#### Present

```c++
vkwsi_transfer_info transfer = { image, layout };
VkResult res = vkwsi_present(vkwsi, &transfer, 1, queue, &render_complete, 1);
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

The library is available as a simple CMake project. Simply add and link against the `vkwsi::vkwsi` target (prefer the alias over the internal underlying `vkwsi` target).

A C++20 capable compiler is required to build the library.

## Example

Configure with `-DVKWSI_BUILD_TESTS=ON` to build the example program (this will fetch and build SDL3)

A C++23 capable compiler is required to build the example.

## Future Work

 - Present timing
 - Threaded acquisition and present
