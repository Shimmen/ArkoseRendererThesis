#pragma once

#include "../RenderGraphNode.h"
#include "RTData.h"
#include "utility/Model.h"
#include "utility/Scene.h"

class RTAmbientOcclusion final : public RenderGraphNode {
public:
    explicit RTAmbientOcclusion(const Scene&);
    ~RTAmbientOcclusion() override = default;

    static std::string name();

    ExecuteCallback constructFrame(Registry&) const override;
};
