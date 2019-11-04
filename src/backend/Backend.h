#pragma once

#include "frontend/CommandQueue.h"
#include "resourceid.h"
#include <string>

class Backend {
public:
    Backend() = default;
    virtual ~Backend() = default;

    virtual ShaderID loadShader(const std::string& shaderName) = 0;
    virtual bool compileCommandQueue(const CommandQueue&) = 0;

    virtual bool executeFrame() = 0;

protected:
    enum class ShaderStageType {
        Unknown,
        VertexShader,
        FragmentShader
    };
    [[nodiscard]] virtual ShaderStageType stageTypeForShaderName(const std::string&) const;
};
