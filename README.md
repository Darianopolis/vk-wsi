# Vulkan WSI Library

This library aims to be a general purpose abstraction layer around the Vulkan WSI interface.

## Goals

- Be correct! And make mistakes easy to diagnose
- Handle the diverse range of gpu driver, toolkit, operating system and display server interactions reliably
- Adapt the legacy binary semaphore interface onto timeline sempahores
- Utilize present fences for *correct* swapchain resource lifetime management (no more "best effort" synchronization)
- Use an optimal number of semaphores and fences to achieve highest throughput and minimal latency on presentation
- Avoid requiring any explicit concept of a "frame"

## Non-Goals

- Support every possible combination of supported Vulkan extensions.
    - Too much core functionality relies on the presence of a handful extensions. All of which should be present on any maintained driver and do not have any notable hardware requirements.
- 100% API coverage of the underlying WSI interface
    - There is too much flexibility to wrap practically while still meeting the above goals.

## Motivation

Windowing System Integration (WSI) is notoriously finnicky to get right. There is a huge combinatorial explosion of drivers, toolkits, and graphical environments:

- Drivers: Nvidia, Intel, AMD; integrated, discrete, and hybrid
- Toolkits: SDL, GLFW, Qt, GTK, raw libwayland, XCB, XLib
- Protocols: Windows' DWM, MacOS' Cocoa, X11, Wayland, Direct Display

And on Linux you also have a huge variety of compositors with varying capabilities and interactions with clients.
- Compositors: KDE Plasma, GNOME, Hyprland, Sway, Niri, etc...

## Vulkan Extensions

A handful of extensions are required, there are no optional extensions in the interest of keeping a focus on the core functionality of the library. All these extensions should be widely available on any actively supported hardware, and there are no particular hardware requirements.

#### Instance

- `VK_KHR_get_surface_capabilities2`
- `VK_EXT_SURFACE_MAINTENANCE`
- `VK_EXT_surface_maintenance1`

#### Device

- `VK_KHR_swapchain` (duhh)
- `VK_EXT_swapchain_maintenance1` - `swapchainMaintenance1`
- `VK_KHR_timeline_semaphores` or Vulkan `1.2` - `timelineSemaphore`
- `VK_KHR_synchronization2` or Vulkan `1.3` - `synchronization2`

## Platform Support

Platforms that have been tested on:

- Linux
    - Arch Linux + KDE Plasma Wayland session (Wayland + X11 via XWayland)
- Windows
    - Windows 11

Other platforms (including Apple and Android devices) may work just fine provided they have the required extensions, but have not been tested for.

## Building

The library is available as a simple CMake project. Simply add and link against the `vk-wsi::vk-wsi` target (prefer the alias over the internal underlying `vk-wsi` target).

You will need a C++20 capable compiler to build the library.

Pass/Enable `-DVKWSI_BUILD_TESTS=ON` to build the example program (this will fetch SDL)
