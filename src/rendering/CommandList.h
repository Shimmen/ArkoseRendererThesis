#pragma once

#include "rendering/Resources.h"

class CommandList {
public:
    virtual void updateBufferImmediately(Buffer&, void*, size_t) = 0;

    virtual void setRenderState(const RenderState&, ClearColor, float clearDepth, uint32_t clearStencil = 0) = 0;
    virtual void setRayTracingState(const RayTracingState&) = 0;

    virtual void bindSet(BindingSet&, uint32_t index) = 0;

    virtual void draw(Buffer& vertexBuffer, uint32_t vertexCount) = 0;
    virtual void drawIndexed(Buffer& vertexBuffer, Buffer& indexBuffer, uint32_t indexCount, uint32_t instanceIndex = 0) = 0;

    virtual void traceRays(Extent2D) = 0;

    //! A barrier for all commands and memory, which probably only should be used for debug stuff.
    virtual void debugBarrier() = 0;
};
