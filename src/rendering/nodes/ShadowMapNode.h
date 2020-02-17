#pragma once

#include "rendering/RenderGraphNode.h"
#include "utility/FpsCamera.h"
#include "utility/Model.h"

class ShadowMapNode {
public:
    static std::string name();
    static RenderGraphBasicNode::ConstructorFunction construct(const Scene&);

private:
    struct Drawable {
        const Mesh* mesh {};
        Buffer* vertexBuffer;
        Buffer* indexBuffer;
        uint32_t indexCount;
    };

    static void setupDrawables(const Scene&, Registry&, std::vector<Drawable>&);
};
