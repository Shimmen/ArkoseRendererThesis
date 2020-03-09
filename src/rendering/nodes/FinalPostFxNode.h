#pragma once

#include "../RenderGraphNode.h"
#include "utility/Scene.h"

class FinalPostFxNode final : public RenderGraphNode {
public:
    explicit FinalPostFxNode(const Scene&);
    ~FinalPostFxNode() override = default;

    static std::string name();
    ExecuteCallback constructFrame(Registry&) const override;

private:
    const Scene& m_scene;
};
