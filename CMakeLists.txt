cmake_minimum_required(VERSION 3.7 FATAL_ERROR)
project(ArkoseRenderer)

set(CMAKE_CXX_STANDARD 17)

add_executable(ArkoseRenderer
        src/main.cpp
        src/app/App.cpp
        src/backend/Backend.cpp
        src/backend/vulkan/VulkanBackend.cpp
        src/utility/fileio.cpp
        src/utility/Random.cpp)

target_include_directories(ArkoseRenderer PRIVATE src/)
target_include_directories(ArkoseRenderer PRIVATE deps/linalg)

set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/glfw-3.3)
target_link_libraries(ArkoseRenderer glfw)

find_package(Vulkan REQUIRED)
target_include_directories(ArkoseRenderer PRIVATE Vulkan::Vulkan)
target_link_libraries(ArkoseRenderer Vulkan::Vulkan)

if (WIN32)
    target_compile_definitions(ArkoseRenderer PRIVATE VK_USE_PLATFORM_WIN32_KHR)
else ()
    message(SEND_ERROR "TODO: Setup vulkan for this platform!")
endif ()
