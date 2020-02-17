#pragma once

#include "../RenderGraphNode.h"

class FinalPostFxNode final : public RenderGraphNode {
public:
    FinalPostFxNode();

    static std::string name();
    ExecuteCallback constructFrame(Registry&) const override;
};
