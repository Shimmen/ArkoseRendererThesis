#pragma once

#include "RenderState.h"
#include "Resources.h"
#include "Shader.h"
#include <memory>
#include <utility>

struct FrontendCommand {

    FrontendCommand() = default;
    virtual ~FrontendCommand() = default;

    [[nodiscard]] const std::type_info& type() const
    {
        return typeid(*this);
    }
};

enum class VertexAttributeType {
    Float2,
    Float3,
    Float4
};

struct VertexAttribute {
    int location {};
    VertexAttributeType type {};
    size_t memoryOffset {};
};

struct VertexLayout {
    size_t vertexStride {};
    std::vector<VertexAttribute> attributes {};
};

enum class DrawMode {
    Triangles
};

struct CmdDrawIndexed : public FrontendCommand {

    CmdDrawIndexed(Buffer& vBuf, VertexLayout vLay, Buffer& iBuf, size_t numI, DrawMode dMode, RenderState state, Shader& shad)
        : vertexBuffer(vBuf)
        , vertexLayout(std::move(vLay))
        , indexBuffer(iBuf)
        , numIndices(numI)
        , mode(dMode)
        , renderState(state)
        , shader(shad)
    {
    }

    Buffer& vertexBuffer;
    VertexLayout vertexLayout;

    Buffer& indexBuffer;
    size_t numIndices;
    DrawMode mode { DrawMode::Triangles };

    RenderState renderState;
    Shader& shader;
};

struct ClearColor {
    float r { 0.0f };
    float g { 0.0f };
    float b { 0.0f };
    float a { 0.0f };
};

struct CmdClear : public FrontendCommand {
    CmdClear(ClearColor color, float depth, uint32_t stencil = 0)
        : clearColor(color)
        , clearDepth(depth)
        , clearStencil(stencil)
    {
    }

    ClearColor clearColor;
    float clearDepth;
    uint32_t clearStencil;
};
