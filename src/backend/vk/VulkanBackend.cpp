#include "VulkanBackend.h"

#include "utility/fileio.h"

VulkanBackend::VulkanBackend(VkInstance instance, VkDevice device)
    : m_instance(instance)
    , m_device(device)
{
}

VulkanBackend::~VulkanBackend()
{
    // TODO: Should this own e.g. the instance and device?
/*
    for (const auto& shaderModule : m_shaderModules) {
        vkDestroyShaderModule(m_device, shaderModule, nullptr);
    }
*/
}

ShaderID VulkanBackend::loadShader(const std::string& shaderName)
{
    ShaderStageType stageType = stageTypeForShaderName(shaderName);
    if (stageType == ShaderStageType::Unknown) {
        LogError("VulkanBackend::loadShader(): unknown shader stage type for shader name '%s'.\n", shaderName.c_str());
        return NullShaderID;
    }
/*
    auto mapResult = m_shaderIdForName.find(shaderName);
    if (mapResult != m_shaderIdForName.end()) {
        return mapResult->second;
    }
*/
    auto fileName = fileNameForShaderName(shaderName);
    auto optionalData = fileio::loadEntireFileAsByteBuffer(fileName);
    if (!optionalData.has_value()) {
        LogError("VulkanBackend::loadShader(): could not load '%s'.\n", shaderName.c_str());
        return NullShaderID;
    }

    const auto& binaryData = optionalData.value();

    VkShaderModuleCreateInfo moduleCreateInfo = {};
    moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleCreateInfo.codeSize = binaryData.size();
    moduleCreateInfo.pCode = reinterpret_cast<const uint32_t*>(binaryData.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_device, &moduleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        LogError("VulkanBackend::loadShader(): could not create shader module.\n");
        return NullShaderID;
    }
/*
    size_t index = m_shaderModules.size();
    auto shaderId = static_cast<ShaderID>(index);
    m_shaderModules.push_back(shaderModule);
*/
    VkPipelineShaderStageCreateInfo stageCreateInfo = {};
    stageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageCreateInfo.stage = vulkanShaderShaderStageFlag(stageType);
    stageCreateInfo.module = shaderModule;
    stageCreateInfo.pName = "main"; // TODO: Allow specifying entry point!
    stageCreateInfo.pSpecializationInfo = nullptr; // TODO: Allow setting constants!

    //m_shaderIdForName.insert({ shaderName, shaderId });

    //return shaderId;
    return NullShaderID; // TODO!
}

bool VulkanBackend::commitAndExecuteCommandQueue(const CommandQueue&)
{
    // TODO!
    return false;
}

std::string VulkanBackend::fileNameForShaderName(const std::string& shaderName) const
{
    return shaderName + ".spv";
}

VkShaderStageFlagBits VulkanBackend::vulkanShaderShaderStageFlag(ShaderStageType type) const
{
    switch (type) {
    case ShaderStageType::VertexShader:
        return VK_SHADER_STAGE_VERTEX_BIT;
    case ShaderStageType::FragmentShader:
        return VK_SHADER_STAGE_FRAGMENT_BIT;
    case ShaderStageType::Unknown:
        ASSERT_NOT_REACHED();
    }
    ASSERT_NOT_REACHED();
}
