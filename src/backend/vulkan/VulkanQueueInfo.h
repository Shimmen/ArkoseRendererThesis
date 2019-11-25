#pragma once

#include <cstdint>

struct VulkanQueueInfo {
    uint32_t graphicsQueueFamilyIndex;
    uint32_t computeQueueFamilyIndex;
    uint32_t presentQueueFamilyIndex;

    [[nodiscard]] bool combinedGraphicsComputeQueue() const
    {
        return graphicsQueueFamilyIndex == computeQueueFamilyIndex;
    }
};
