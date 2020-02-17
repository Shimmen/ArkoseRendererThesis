#pragma once

#include "rendering/RenderGraphNode.h"
#include "utility/FpsCamera.h"
#include "utility/Model.h"

class ShadowMapNode final : public RenderGraphNode {
public:
    explicit ShadowMapNode(const Scene&);
    ~ShadowMapNode() override = default;

    static std::string name();

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

private:
    struct Drawable {
        const Mesh* mesh {};
        Buffer* vertexBuffer {};
        Buffer* indexBuffer {};
        uint32_t indexCount {};
    };

    const Scene& m_scene;
    std::vector<Drawable> m_drawables {};
};
