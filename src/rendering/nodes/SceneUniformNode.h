#pragma once

#include "../RenderGraphNode.h"
#include "utility/Model.h"

class SceneUniformNode final : public RenderGraphNode {
public:
    explicit SceneUniformNode(const Scene&);

    static std::string name();
    ExecuteCallback constructFrame(Registry&) const override;

private:
    const Scene& m_scene;
};
