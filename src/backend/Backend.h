#pragma once

#include "common.h"
#include "frontend/CommandQueue.h"
#include "resourceid.h"

class Backend {
public:
    virtual ShaderID loadShader(const std::string& shaderName) = 0;
    virtual bool compileCommandQueue(const CommandQueue&) = 0;

    virtual void executeFrame() = 0;

protected:
    enum class ShaderStageType {
        Unknown,
        VertexShader,
        FragmentShader
    };
    [[nodiscard]] virtual ShaderStageType stageTypeForShaderName(const std::string&) const;
};
