// #pragma once

// #include "vk-wsi.hpp"

// #include <SDL3/SDL.h>
// #include <SDL3/SDL_vulkan.h>

// #include <print>
// #include <iostream>
// #include <source_location>

// // -----------------------------------------------------------------------------

// template<typename... Args>
// [[noreturn]] void error(std::source_location loc, std::format_string<Args...> fmt, Args&& ...args)
// {
//     std::cout << std::format("ERROR({} @ {}):\n", loc.line(), loc.file_name());
//     std::cout << std::vformat(fmt.get(), std::make_format_args(args...)) << '\n';
//     std::cin.get();
//     std::exit(1);
// }

// struct LocatedVkResult
// {
//     VkResult res;
//     std::source_location loc;

//     LocatedVkResult(VkResult _res, std::source_location _loc = std::source_location::current())
//         : res(_res), loc(_loc)
//     {}
// };

// VkResult vk_check(LocatedVkResult located_res, auto... allowed)
// {
//     auto[res, loc] = located_res;
//     if (res == VK_SUCCESS || (... || (res == allowed))) return res;
//     error(loc, "VkResult = {}", int(res));
// }

// template<typename Container, typename Fn, typename... Args>
// void vk_enumerate(Container& container, Fn&& fn, Args&&... args)
// {
//     uint32_t count = static_cast<uint32_t>(container.size());
//     for (;;) {
//         uint32_t old_count = count;
//         if constexpr (std::same_as<VkResult, decltype(fn(args..., &count, nullptr))>) {
//             vk_check(fn(args..., &count, container.data()), VK_INCOMPLETE);
//         } else {
//             fn(args..., &count, container.data());
//         }

//         container.resize(count);
//         if (count <= old_count) return;
//     }
// }

// auto ptr_to(auto&& v) { return &v; }

// #define vk_instance_fn(fn) PFN_##fn fn = reinterpret_cast<PFN_##fn>(vkGetInstanceProcAddr(instance, #fn)); \
//     if (!fn) error(std::source_location::current(), "Instance function " #fn " failed to load!")

// #define vk_device_fn(fn) PFN_##fn fn = reinterpret_cast<PFN_##fn>(vkGetDeviceProcAddr(device, #fn)); \
//     if (!fn) error(std::source_location::current(), "Device function " #fn " failed to load!")

// // -----------------------------------------------------------------------------

// struct vkwsi_test
// {
//     VkInstance instance;
//     VkDevice device;
//     uint32_t queue_family;
//     VkQueue queue;

//     VkFence fence;
// };

// void vkwsi_test_init(vkwsi_test* test);