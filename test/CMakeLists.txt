include(FetchContent)

FetchContent_Declare(SDL
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG main)
FetchContent_MakeAvailable(SDL)

FetchContent_Declare(GLFW
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG master)
FetchContent_MakeAvailable(GLFW)

add_executable(vk-wsi-test)

# target_compile_options(vk-wsi-test PUBLIC
#     -fsanitize=address,undefined
#     )
# target_link_libraries(vk-wsi-test PUBLIC
#     asan
#     ubsan
#     )

target_compile_features(vk-wsi-test PUBLIC cxx_std_20)
target_sources(vk-wsi-test PUBLIC
    vk-wsi-test.cpp
    )
target_link_libraries(vk-wsi-test PUBLIC
    vk-wsi::vk-wsi
    SDL3::SDL3
    glfw
    )
