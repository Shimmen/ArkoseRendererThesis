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

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

private:
    const Scene& m_scene;

    Texture* m_accumulatedAO;
    mutable uint32_t m_numAccumulatedFrames { 0 };
};
