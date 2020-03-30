#pragma once

#include "../RenderGraphNode.h"
#include "RTData.h"
#include "utility/Model.h"
#include "utility/Scene.h"

class RTFirstHitNode final : public RenderGraphNode {
public:
    explicit RTFirstHitNode(const Scene&);
    ~RTFirstHitNode() override = default;

    static std::string name();

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

private:
    Buffer& createTriangleMeshVertexBuffer(const Mesh&) const;

private:
    const Scene& m_scene;
    BindingSet* m_objectDataBindingSet {};
};
