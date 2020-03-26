#pragma once

#include "../RenderGraphNode.h"
#include "RTData.h"
#include "utility/Model.h"
#include "utility/Scene.h"
#include <random>

class RTDiffuseGINode final : public RenderGraphNode {
public:
    explicit RTDiffuseGINode(const Scene&);
    ~RTDiffuseGINode() override = default;

    static std::string name();

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

private:
    const Scene& m_scene;

    Texture* m_accumulationTexture;
    mutable uint32_t m_numAccumulatedFrames { 0 };

    mutable std::mt19937_64 m_randomGenerator;
    mutable std::uniform_real_distribution<float> m_bilateral { -1.0f, +1.0f };

    BindingSet* m_objectDataBindingSet {};
};
