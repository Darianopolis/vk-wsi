cmake_minimum_required(VERSION 4.0)
project(vkwsi)

# ------------------------------------------------------------------------------

find_package(Vulkan REQUIRED)

# ------------------------------------------------------------------------------

add_library(vkwsi)
target_compile_features(vkwsi PRIVATE cxx_std_20)
target_sources(vkwsi PRIVATE
    src/vkwsi-context.cpp
    src/vkwsi-acquire.cpp
    src/vkwsi-present.cpp
    )
target_include_directories(vkwsi PUBLIC include)
target_link_libraries(vkwsi PUBLIC Vulkan::Headers)

add_library(vkwsi::vkwsi ALIAS vkwsi)

# ------------------------------------------------------------------------------

option(VKWSI_BUILD_TESTS "Build the vkwsi tests/examples" OFF)

if (VKWSI_BUILD_TESTS)
    add_subdirectory(test)
endif()
