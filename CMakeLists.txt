cmake_minimum_required(VERSION 4.0)
project(vk-wsi)

# ------------------------------------------------------------------------------

find_package(Vulkan REQUIRED)

# ------------------------------------------------------------------------------

add_library(vk-wsi)
target_compile_features(vk-wsi PUBLIC cxx_std_26)
target_sources(vk-wsi PUBLIC src/vk-wsi.cpp)
target_include_directories(vk-wsi PUBLIC include)

# ------------------------------------------------------------------------------

option(VKWSI_BUILD_TESTS "Build the vk-wise tests/examples" OFF)

if (VKWSI_BUILD_TESTS)
    add_subdirectory(test)
endif()
