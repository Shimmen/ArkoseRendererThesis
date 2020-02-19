#pragma once

#include <optional>
#include <vulkan/vulkan.h>

class VulkanRTX {
public:
    explicit VulkanRTX(VkPhysicalDevice, VkDevice);

    static bool isSupportedOnPhysicalDevice(VkPhysicalDevice);

    PFN_vkCreateAccelerationStructureNV vkCreateAccelerationStructureNV { nullptr };
    PFN_vkDestroyAccelerationStructureNV vkDestroyAccelerationStructureNV { nullptr };
    PFN_vkBindAccelerationStructureMemoryNV vkBindAccelerationStructureMemoryNV { nullptr };
    PFN_vkGetAccelerationStructureHandleNV vkGetAccelerationStructureHandleNV { nullptr };
    PFN_vkGetAccelerationStructureMemoryRequirementsNV vkGetAccelerationStructureMemoryRequirementsNV { nullptr };
    PFN_vkCmdBuildAccelerationStructureNV vkCmdBuildAccelerationStructureNV { nullptr };
    PFN_vkCreateRayTracingPipelinesNV vkCreateRayTracingPipelinesNV { nullptr };
    PFN_vkGetRayTracingShaderGroupHandlesNV vkGetRayTracingShaderGroupHandlesNV { nullptr };
    PFN_vkCmdTraceRaysNV vkCmdTraceRaysNV { nullptr };

    const VkPhysicalDeviceRayTracingPropertiesNV& properties() const;

private:
    VkPhysicalDevice m_physicalDevice;
    VkDevice m_device;

    VkPhysicalDeviceRayTracingPropertiesNV m_rayTracingProperties { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_NV };
};
