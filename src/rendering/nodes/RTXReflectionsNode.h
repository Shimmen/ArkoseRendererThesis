#pragma once

#include "../RenderGraphNode.h"
#include "utility/Model.h"

class RTXReflectionsNode final : public RenderGraphNode {
public:
    explicit RTXReflectionsNode(const Scene&);
    ~RTXReflectionsNode() override = default;

    static std::string name();

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

private:
    const Scene& m_scene;
};
