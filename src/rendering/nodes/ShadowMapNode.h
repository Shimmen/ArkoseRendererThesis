#pragma once

#include "rendering/RenderGraphNode.h"
#include "utility/FpsCamera.h"
#include "utility/Model.h"

class ShadowMapNode {
public:
    static std::string name();
    static NEWBasicRenderGraphNode::ConstructorFunction construct(const Scene&);

private:
    struct Drawable {
        const Mesh* mesh {};
        Buffer* vertexBuffer;
        Buffer* indexBuffer;
        uint32_t indexCount;
    };

    static void setupDrawables(const Scene&, ResourceManager&, std::vector<Drawable>&);
};
