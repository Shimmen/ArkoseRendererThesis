#pragma once

#include "../RenderGraphNode.h"
#include "utility/Scene.h"

class RTAccelerationStructures final : public RenderGraphNode {
public:
    explicit RTAccelerationStructures(const Scene&);
    ~RTAccelerationStructures() override = default;

    static std::string name();

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

private:
    const Scene& m_scene;
    std::vector<RTGeometryInstance> m_instances {};
};
