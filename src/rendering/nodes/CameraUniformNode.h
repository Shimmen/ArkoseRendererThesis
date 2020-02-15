#pragma once

#include "../RenderGraphNode.h"
#include "utility/FpsCamera.h"

class CameraUniformNode {
public:
    static std::string name();
    static NEWBasicRenderGraphNode::ConstructorFunction construct(const FpsCamera&);
};
