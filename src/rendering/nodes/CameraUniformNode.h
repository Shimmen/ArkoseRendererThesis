#pragma once

#include "../RenderGraphNode.h"
#include "utility/FpsCamera.h"

class CameraUniformNode final : public RenderGraphNode {
public:
    explicit CameraUniformNode(const FpsCamera&);

    static std::string name();
    ExecuteCallback constructFrame(Registry&) const override;

private:
    const FpsCamera* m_fpsCamera;
};
