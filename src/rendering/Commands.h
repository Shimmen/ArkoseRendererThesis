#pragma once

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

enum class DrawMode {
    Triangles
};

struct CmdDrawIndexed : public FrontendCommand {

    CmdDrawIndexed(Buffer& vBuf, Buffer& iBuf, size_t numI, DrawMode dMode)
        : vertexBuffer(vBuf)
        , indexBuffer(iBuf)
        , numIndices(numI)
        , mode(dMode)
    {
    }

    Buffer& vertexBuffer;

    Buffer& indexBuffer;
    size_t numIndices;
    DrawMode mode { DrawMode::Triangles };
};

struct CmdSetRenderState : public FrontendCommand {

    CmdSetRenderState(RenderState& renderState)
        : renderState(renderState)
    {
    }

    RenderState& renderState;
};

struct CmdUpdateBuffer : public FrontendCommand {

    CmdUpdateBuffer(Buffer& buffer, void* source, size_t size, size_t offset = 0u)
        : buffer(buffer)
        , source(source)
        , size(size)
        , offset(offset)
    {
    }

    Buffer& buffer;
    void* source;
    size_t size;
    size_t offset;
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
