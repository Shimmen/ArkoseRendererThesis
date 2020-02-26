#pragma once

#include "../RenderGraphNode.h"
#include "RTData.h"
#include "utility/Model.h"

class RTFirstHitNode final : public RenderGraphNode {
public:
    explicit RTFirstHitNode(const Scene&);
    ~RTFirstHitNode() override = default;

    static std::string name();

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

private:
    const Scene& m_scene;
    std::vector<RTGeometryInstance> m_instances {};

    std::vector<const Buffer*> m_vertexBuffers {};
    std::vector<const Buffer*> m_indexBuffers {};
    std::vector<RTMesh> m_rtMeshes {};

    BindingSet* m_textureBindingSet {};
};
