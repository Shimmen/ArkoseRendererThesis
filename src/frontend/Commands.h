#pragma once

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
    std::unique_ptr<Buffer> vertexBuffer;
    VertexLayout vertexLayout;

    std::unique_ptr<Buffer> indexBuffer;

    std::unique_ptr<Shader> shader;

    size_t numIndices;
    DrawMode mode { DrawMode::Triangles };
};

}
