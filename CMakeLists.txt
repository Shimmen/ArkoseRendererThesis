cmake_minimum_required(VERSION 3.7 FATAL_ERROR)
project(ArkoseRenderer)

set(CMAKE_CXX_STANDARD 17)

add_executable(ArkoseRenderer
        src/app/TestApp.cpp
        src/main.cpp
        src/backend/vulkan/VulkanBackend.cpp
        src/rendering/Shader.cpp
        src/rendering/ShaderManager.cpp
        src/rendering/ResourceManager.cpp
        src/rendering/StaticResourceManager.cpp
        src/rendering/Resources.cpp
        src/rendering/RenderGraphNode.cpp
        src/rendering/RenderGraph.cpp
        src/utility/FrameAllocator.cpp
        src/utility/GlobalState.cpp
        src/utility/FpsCamera.cpp
        src/utility/Input.cpp
        src/utility/fileio.cpp
        src/utility/Random.cpp)

target_compile_options(ArkoseRenderer PRIVATE -Wall)
target_compile_options(ArkoseRenderer PRIVATE -Wimplicit-fallthrough)

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

set(SHADERC_SKIP_TESTS CACHE BOOL TRUE)
set(EFFCEE_BUILD_TESTING CACHE BOOL FALSE FORCE)
add_subdirectory(deps/shaderc)
target_include_directories(ArkoseRenderer PUBLIC ${shaderc_SOURCE_DIR}/libshaderc_util/include)
target_link_libraries(ArkoseRenderer shaderc)

add_subdirectory(deps/stb_image)
target_link_libraries(ArkoseRenderer stb_image)

add_subdirectory(deps/tiny_gltf)
target_link_libraries(ArkoseRenderer tiny_gltf)

find_package(Vulkan REQUIRED)
target_include_directories(ArkoseRenderer PRIVATE Vulkan::Vulkan)
target_link_libraries(ArkoseRenderer Vulkan::Vulkan)

add_subdirectory(deps/vulkan_memory_allocator)
target_link_libraries(ArkoseRenderer vulkan_memory_allocator)

if (WIN32)
    target_compile_definitions(ArkoseRenderer PRIVATE VK_USE_PLATFORM_WIN32_KHR)
endif ()
