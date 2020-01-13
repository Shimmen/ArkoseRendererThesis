#pragma once

#include "Resources.h"
#include "Shader.h"
#include <memory>
#include <utility>

struct FrontendCommand {

    FrontendCommand() = default;
    virtual ~FrontendCommand() = default;

    template<typename T>
    bool is() const
    {
        return typeid(*this) == typeid(T);
    }

    template<typename T>
    const T& as() const
    {
        ASSERT(is<T>());
        auto* casted = dynamic_cast<const T*>(this);
        return *casted;
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

    explicit CmdSetRenderState(RenderState& renderState)
        : renderState(renderState)
    {
    }

    RenderState& renderState;
};

struct CmdUpdateBuffer : public FrontendCommand {

    CmdUpdateBuffer(Buffer& buffer, void* source, size_t size)
        : buffer(buffer)
        , source(source)
        , size(size)
    {
    }

    Buffer& buffer;
    void* source;
    size_t size;
};

struct ClearColor {
    ClearColor(float r, float g, float b, float a = 1.0f)
        : r(r)
        , g(g)
        , b(b)
        , a(a)
    {
    }

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
