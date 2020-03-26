cmake_minimum_required(VERSION 3.8 FATAL_ERROR)
project(ArkoseRenderer)

include(FetchContent)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

add_executable(ArkoseRenderer
        src/apps/TestApp.cpp
        src/main.cpp
        src/backend/vulkan/VulkanBackend.cpp
        src/backend/vulkan/VulkanCore.cpp
        src/backend/vulkan/VulkanCommandList.cpp
        src/backend/vulkan/VulkanRTX.cpp
        src/rendering/Shader.cpp
        src/rendering/ShaderManager.cpp
        src/rendering/Registry.cpp
        src/rendering/Resources.cpp
        src/rendering/RenderGraphNode.cpp
        src/rendering/RenderGraph.cpp
        src/rendering/nodes/SceneUniformNode.cpp
        src/rendering/nodes/FinalPostFxNode.cpp
        src/rendering/nodes/ForwardRenderNode.cpp
        src/rendering/nodes/SlowForwardRenderNode.cpp
        src/rendering/nodes/ShadowMapNode.cpp
        src/rendering/nodes/RTAccelerationStructures.cpp
        src/rendering/nodes/RTFirstHitNode.cpp
        src/rendering/nodes/RTReflectionsNode.cpp
        src/rendering/nodes/RTDiffuseGINode.cpp
        src/utility/GlobalState.cpp
        src/utility/models/GltfModel.cpp
        src/utility/Scene.cpp
        src/utility/FpsCamera.cpp
        src/utility/Input.cpp
        src/utility/FileIO.cpp
        src/utility/Random.cpp)

# (C++20 is required for designated initializers in VC++)
target_compile_features(ArkoseRenderer PRIVATE cxx_std_20)

if (MSVC)
    add_compile_options(/MT)
    add_compile_options(/Wall)
    target_compile_options(ArkoseRenderer PRIVATE /MT)
    target_compile_options(ArkoseRenderer PRIVATE /Wall)
    target_link_options(ArkoseRenderer PRIVATE /PROFILE)
else()
    target_compile_options(ArkoseRenderer PRIVATE -Wall)
    target_compile_options(ArkoseRenderer PRIVATE -Wimplicit-fallthrough)
endif()

target_include_directories(ArkoseRenderer PRIVATE src/)
target_include_directories(ArkoseRenderer PRIVATE shaders/shared)

target_include_directories(ArkoseRenderer PRIVATE deps/glm-0.9.9.6)
target_include_directories(ArkoseRenderer PRIVATE deps/nlohmann_json)

FetchContent_Declare(spirvcross
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
    GIT_TAG        "2020-02-26"
  )
FetchContent_GetProperties(spirvcross)
if(NOT spirvcross_POPULATED)
    FetchContent_Populate(spirvcross)
    add_subdirectory(${spirvcross_SOURCE_DIR} ${spirvcross_BINARY_DIR})
endif()
target_link_libraries(ArkoseRenderer spirv-cross-core spirv-cross-glsl spirv-cross-reflect)

# TODO: Look into using https://github.com/Kitware/CMake/blob/master/Modules/FetchContent.cmake
#  here instead! I tried but there was something weird, possibly with the newer gltf source..
#  Anyway, probably try sometime again in the future! Would be great to not have git submodules.
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
target_link_libraries(ArkoseRenderer Vulkan::Vulkan)

find_package(VulkanSDK REQUIRED)
target_link_libraries(ArkoseRenderer ${shaderc_combined_LIB})

add_subdirectory(deps/vulkan_memory_allocator)
target_link_libraries(ArkoseRenderer vulkan_memory_allocator)

add_subdirectory(deps/dear-imgui)
target_link_libraries(ArkoseRenderer dear_imgui)

if (WIN32)
    target_compile_definitions(ArkoseRenderer PRIVATE VK_USE_PLATFORM_WIN32_KHR)
endif ()
