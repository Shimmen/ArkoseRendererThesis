#pragma once

#include "../RenderGraphNode.h"
#include "utility/Model.h"

class RTReflectionsNode final : public RenderGraphNode {
public:
    explicit RTReflectionsNode(const Scene&);
    ~RTReflectionsNode() override = default;

    static std::string name();

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

private:
    const Scene& m_scene;
    TopLevelAS* m_tlas;
};
