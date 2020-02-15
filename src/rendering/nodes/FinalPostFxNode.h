#pragma once

#include "../RenderGraphNode.h"

class FinalPostFxNode {
public:
    static std::string name();
    static NEWBasicRenderGraphNode::ConstructorFunction construct();
};
