#pragma once

#include "common.h"
#include "resourceid.h"

#include "frontend/CommandQueue.h"

class Backend {
public:
    virtual ShaderID loadShader(const std::string& shaderName) = 0;
    virtual bool commitAndExecuteCommandQueue(const CommandQueue&) = 0;

protected:
    enum class ShaderStageType {
        Unknown,
        VertexShader,
        FragmentShader
    };
    [[nodiscard]] virtual ShaderStageType stageTypeForShaderName(const std::string&) const;
};
