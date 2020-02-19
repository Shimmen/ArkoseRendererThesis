#pragma once

#include "../RenderGraphNode.h"
#include "ForwardData.h"
#include "utility/FpsCamera.h"
#include "utility/Model.h"

class SlowForwardRenderNode final : public RenderGraphNode {
public:
    explicit SlowForwardRenderNode(const Scene&);

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

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

    std::vector<Drawable> m_drawables {};
    const Scene& m_scene;
};
