#pragma once

#include "common.h"

#include <vulkan/vulkan.h>

#ifdef NDEBUG
constexpr bool vulkanDebugMode = false;
#else
constexpr bool vulkanDebugMode = true;
#endif
