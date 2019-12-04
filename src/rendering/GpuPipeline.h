#pragma once

#include "ApplicationState.h"
#include "RenderPass.h"
#include "ResourceManager.h"
#include <memory>
#include <string>
#include <unordered_map>

class GpuPipeline {
public:
    GpuPipeline() = default;
    ~GpuPipeline() = default;

    NON_COPYABLE(GpuPipeline);
    GpuPipeline(GpuPipeline&&) noexcept = default;

    bool needsReconstruction(const ApplicationState&) const;
    void constructAll(ResourceManager&);

    // TODO: Add function for getting iterator(?) or some way of iterating all passes in a valid order!

    void addRenderPass(const std::string& name, const RenderPass::RenderPassConstructorFunction&);
    void addRenderPass(const std::string& name, std::unique_ptr<RenderPass>);

    // TODO: Functions for adding compute passes etc. should also be here!

private:
    std::unordered_map<std::string, std::unique_ptr<RenderPass>> m_renderPasses;
};
