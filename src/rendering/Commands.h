#pragma once

#include "Resources.h"
#include "Shader.h"
#include "utility/logging.h"
#include <memory>

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

struct CmdDrawArray : public FrontendCommand {

    CmdDrawArray(Buffer& vertexBuffer, size_t vertexCount, DrawMode mode)
        : vertexBuffer(vertexBuffer)
        , vertexCount(vertexCount)
        , mode(mode)
    {
        switch (mode) {
        case DrawMode::Triangles:
            if (vertexCount < 3 || (vertexCount % 3) != 0) {
                LogErrorAndExit("CmdDrawArray vertexCount must be at least 3, and a multiple of 3 for DrawMode::Triangles\n");
            }
            break;
        }
    }

    Buffer& vertexBuffer;
    size_t vertexCount;
    DrawMode mode { DrawMode::Triangles };
};

struct CmdDrawIndexed : public FrontendCommand {

    CmdDrawIndexed(Buffer& vBuf, Buffer& iBuf, size_t numI, DrawMode dMode, uint32_t instanceIdx = 0)
        : vertexBuffer(vBuf)
        , indexBuffer(iBuf)
        , numIndices(numI)
        , mode(dMode)
        , instanceIndex(instanceIdx)
    {
    }

    Buffer& vertexBuffer;
    Buffer& indexBuffer;
    size_t numIndices;
    DrawMode mode { DrawMode::Triangles };
    uint32_t instanceIndex;
};

struct CmdSetRenderState : public FrontendCommand {

    explicit CmdSetRenderState(RenderState& renderState)
        : renderState(renderState)
    {
    }

    RenderState& renderState;
};

struct CmdBindSet : public FrontendCommand {

    CmdBindSet(uint32_t index, BindingSet& set)
        : bindingSet(set)
        , setIndex(index)
    {
    }

    BindingSet& bindingSet;
    uint32_t setIndex;
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

struct CmdCopyTexture : public FrontendCommand {

    CmdCopyTexture(const Texture& srcTexture, const Texture& dstTexture)
        : srcTexture(srcTexture)
        , dstTexture(dstTexture)
    {
        ASSERT(srcTexture.extent() == dstTexture.extent());
    }

    const Texture& srcTexture;
    const Texture& dstTexture;
};

struct ClearColor {
    ClearColor(float r, float g, float b, float a = 1.0f)
        : r(pow(r, 2.2f))
        , g(pow(g, 2.2f))
        , b(pow(b, 2.2f))
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
