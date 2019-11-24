cmake_minimum_required(VERSION 3.7 FATAL_ERROR)
project(ArkoseRenderer)

set(CMAKE_CXX_STANDARD 17)

add_executable(ArkoseRenderer
        src/main.cpp
        src/app/App.cpp
        src/backend/ShaderManager.cpp
        src/backend/vulkan/VulkanBackend.cpp
        src/backend/vulkan/VulkanContext.cpp
        src/frontend/Resources.cpp
        src/frontend/RenderPass.cpp
        src/utility/fileio.cpp
        src/utility/Random.cpp)

target_include_directories(ArkoseRenderer PRIVATE src/)
target_include_directories(ArkoseRenderer PRIVATE shaders/shared)

target_include_directories(ArkoseRenderer PRIVATE deps/glm-0.9.9.6)
target_include_directories(ArkoseRenderer PRIVATE deps/nlohmann_json)

set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/glfw-3.3)
target_link_libraries(ArkoseRenderer glfw)

add_subdirectory(deps/stb_image)
target_link_libraries(ArkoseRenderer stb_image)

add_subdirectory(deps/tiny_gltf)
target_link_libraries(ArkoseRenderer tiny_gltf)

find_package(Vulkan REQUIRED)
target_include_directories(ArkoseRenderer PRIVATE Vulkan::Vulkan)
target_link_libraries(ArkoseRenderer Vulkan::Vulkan)

if (WIN32)
    target_compile_definitions(ArkoseRenderer PRIVATE VK_USE_PLATFORM_WIN32_KHR)
endif ()
