#pragma once

#include "../RenderGraphNode.h"
#include "utility/FpsCamera.h"

class CameraUniformNode {
public:
    static RenderGraphNode::NodeConstructorFunction construct(const FpsCamera&);
};
