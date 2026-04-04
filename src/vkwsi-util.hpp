#pragma once

#include "vkwsi.h"

#include <utility>

#define VKWSI_CONCAT_INTERNAL(a, b) a##b
#define VKWSI_CONCAT(a, b) VKWSI_CONCAT_INTERNAL(a, b)
#define VKWSI_UNIQUE_VAR() VKWSI_CONCAT(vkwsi_var_, __COUNTER__)

template<typename fn_t>
struct vkwsi_defer_guard
{
    fn_t fn;

    vkwsi_defer_guard(fn_t&& fn): fn(std::move(fn)) {}
    ~vkwsi_defer_guard() { fn(); };
};

#define VKWSI_DEFER vkwsi_defer_guard VKWSI_UNIQUE_VAR() = [&]

static
auto* vkwsi_ptr_to(auto&& v)
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

#define VKWSI_CHECK(Res) \
    do { \
        if (auto vkwsi_check_res = Res; vkwsi_check_res != VK_SUCCESS) \
            return vkwsi_check_res; \
    } while (0)
