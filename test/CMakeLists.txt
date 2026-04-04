include(FetchContent)

FetchContent_Declare(SDL
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG main)
FetchContent_MakeAvailable(SDL)

# ------------------------------------------------------------------------------

add_executable(         vkwsi-test)
target_compile_options( vkwsi-test PUBLIC -fsanitize=address,undefined )
target_link_libraries(  vkwsi-test PUBLIC asan ubsan)
target_compile_features(vkwsi-test PUBLIC cxx_std_23)

target_sources(vkwsi-test PRIVATE
    vkwsi-test.cpp)
target_link_libraries(vkwsi-test PRIVATE
    vkwsi::vkwsi
    SDL3::SDL3
    glfw)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(vkwsi-test PRIVATE stdc++exp)
endif()
