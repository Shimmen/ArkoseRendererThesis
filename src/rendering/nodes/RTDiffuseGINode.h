#pragma once

#include "../RenderGraphNode.h"
#include "RTData.h"
#include "utility/Model.h"

class RTDiffuseGINode final : public RenderGraphNode {
public:
    explicit RTDiffuseGINode(const Scene&);
    ~RTDiffuseGINode() override = default;

    static std::string name();

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

private:
    const Scene& m_scene;
    std::vector<RTGeometryInstance> m_instances {};

    BindingSet* m_objectDataBindingSet {};
};
