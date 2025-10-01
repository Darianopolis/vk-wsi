include(FetchContent)

FetchContent_Declare(SDL
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG main
    )
FetchContent_Declare(backward-cpp
    GIT_REPOSITORY https://github.com/bombela/backward-cpp.git
    GIT_TAG master
    )
FetchContent_MakeAvailable(SDL backward-cpp)

add_executable(vk-wsi-test)

target_compile_options(vk-wsi-test PUBLIC
    -fsanitize=address,undefined
    )
target_link_libraries(vk-wsi-test PUBLIC
    asan
    ubsan
    )

target_compile_features(vk-wsi PUBLIC cxx_std_26)
target_sources(vk-wsi-test PUBLIC vk-wsi-test.cpp)
target_link_libraries(vk-wsi-test PUBLIC
    vk-wsi::vk-wsi
    SDL3::SDL3
    Backward::Object
    )
