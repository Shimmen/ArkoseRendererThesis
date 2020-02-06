
if (APPLE)
    set(VULKAN_SDK_SEARCH_PATHS /usr/local/lib)
elseif(WIN32)
    # TODO: Implement me!!
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        # todo
    else()
        # todo
    endif()
endif()

find_library(shaderc_combined_LIB
        NAMES shaderc_combined
        PATHS ${VULKAN_SDK_SEARCH_PATHS})

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(VulkanSDK REQUIRED_VARS
        shaderc_combined_LIB)
