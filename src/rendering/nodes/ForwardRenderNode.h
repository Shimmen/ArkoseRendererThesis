#pragma once

#include "../RenderGraphNode.h"
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
    };
};
