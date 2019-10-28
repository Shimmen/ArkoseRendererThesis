#include "Backend.h"

Backend::ShaderStageType Backend::stageTypeForShaderName(const std::string& shaderName) const
{
    if (shaderName.find(".vert") != std::string::npos)
        return ShaderStageType::VertexShader;
    if (shaderName.find(".frag") != std::string::npos)
        return ShaderStageType ::FragmentShader;

    return ShaderStageType::Unknown;
}
