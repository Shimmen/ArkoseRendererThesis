#pragma once

#include "RenderState.h"
#include "Resources.h"
#include <memory>

namespace command {

struct Command {
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

struct DrawElements : public Command {

    DrawElements(Buffer& vBuf, VertexLayout vLay, Buffer& iBuf, size_t numI, DrawMode dMode, RenderState state, Shader& shad)
        : vertexBuffer(vBuf)
        , vertexLayout(vLay)
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

}
