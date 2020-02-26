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

    BindingSet* m_objectDataBindingSet {};
};
