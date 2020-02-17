#pragma once

#include "../RenderGraphNode.h"
#include "ForwardData.h"
#include "utility/FpsCamera.h"
#include "utility/Model.h"

class ForwardRenderNode {
public:
    static std::string name();
    static RenderGraphBasicNode::ConstructorFunction construct(const Scene&);

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

    static void setupState(const Scene&, Registry&, State&);

    static RenderGraphBasicNode::ConstructorFunction constructFastImplementation(const Scene&);
};
