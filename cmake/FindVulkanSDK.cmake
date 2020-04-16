
if (APPLE)
    set(VULKAN_SDK_SEARCH_PATHS /usr/local/lib)
elseif(WIN32)
    # TODO: Don't assume SDK version like this! Do something proper
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(VULKAN_SDK_SEARCH_PATHS "C:/VulkanSDK/1.2.131.1/Lib")
    else()
        set(VULKAN_SDK_SEARCH_PATHS "C:/VulkanSDK/1.2.131.1/Lib32")
    endif()
endif()

find_library(shaderc_combined_LIB
        NAMES shaderc_combined
        PATHS ${VULKAN_SDK_SEARCH_PATHS})

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(VulkanSDK REQUIRED_VARS
        shaderc_combined_LIB)
