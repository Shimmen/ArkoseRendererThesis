#pragma once

#include "../Backend.h"
#include "common-vk.h"
#include "common.h"

class VulkanBackend final : public Backend {
public:
    VulkanBackend(VkInstance, VkDevice);
    virtual ~VulkanBackend();

    ShaderID loadShader(const std::string& shaderName) override;
    bool commitAndExecuteCommandQueue(const CommandQueue&) override;

private:
    [[nodiscard]] std::string fileNameForShaderName(const std::string&) const;
    [[nodiscard]] VkShaderStageFlagBits vulkanShaderShaderStageFlag(ShaderStageType) const;

    VkInstance m_instance;
    VkDevice m_device;

    //std::vector<VkShaderModule> m_shaderModules {};
    //std::unordered_map<std::string, ShaderID> m_shaderIdForName {};
};
