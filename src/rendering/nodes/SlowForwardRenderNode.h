#pragma once

#include "../RenderGraphNode.h"
#include "ForwardData.h"
#include "utility/FpsCamera.h"
#include "utility/Model.h"

class SlowForwardRenderNode {
public:
    static std::string name();
    static NEWBasicRenderGraphNode::ConstructorFunction construct(const Scene&);

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
        Buffer* objectDataBuffer {};
        BindingSet* bindingSet {};
    };

    struct State {
        std::vector<Drawable> drawables {};
    };

    static void setupState(const Scene&, ResourceManager&, State&);
};
