#pragma once

#include "../RenderGraphNode.h"
#include "utility/FpsCamera.h"

class ForwardRenderNode {
public:
    // TODO: All of this is stupid and shouldn't be here
    struct Vertex {
        vec3 position;
        vec3 color;
        vec2 texCoord;
    };
    struct Object {
        Texture* diffuseTexture {};
        Buffer* vertexBuffer {};
        Buffer* indexBuffer {};
        size_t indexCount {};
    };
    struct Scene {
        std::vector<Object> objects {};
        const FpsCamera* camera {};
    };

    static RenderGraphNode::NodeConstructorFunction construct(const Scene&);
};
