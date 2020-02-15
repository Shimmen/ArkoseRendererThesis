#pragma once

#include "../RenderGraphNode.h"
#include "utility/FpsCamera.h"

class CameraUniformNode {
public:
    static std::string name();
    static RenderGraphBasicNode::ConstructorFunction construct(const FpsCamera&);
};
