#pragma once

#include "../RenderGraphNode.h"
#include "ForwardData.h"
#include "rendering/StaticResourceManager.h"
#include "utility/FpsCamera.h"
#include "utility/Model.h"

class ForwardRenderNode {
public:
    static std::string name();
    static RenderGraphNode::NodeConstructorFunction construct(const Scene&, StaticResourceManager&);

private:
    struct Vertex {
        vec3 position;
        vec2 texCoord;
        vec3 normal;
        vec4 tangent;
    };

    struct Drawable {
        const Mesh* mesh {};
        Buffer* vertexBuffer {};
        Buffer* indexBuffer {};
        uint32_t indexCount {};
        int materialIndex {};
    };

    struct State {
        std::vector<Drawable> drawables {};
        std::vector<const Texture*> textures {};
        std::vector<ForwardMaterial> materials {};
    };

    static void setupState(const Scene&, StaticResourceManager&, State&);
};
