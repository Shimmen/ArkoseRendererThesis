#include "GpuPipeline.h"

#include <utility/logging.h>

bool GpuPipeline::needsReconstruction(const ApplicationState& appState) const
{
    for (const auto& [name, renderPass] : m_renderPasses) {
        if (renderPass->needsConstruction(appState)) {
            return true;
        }
    }

    return false;
}

void GpuPipeline::constructAll(ResourceManager& resourceManager)
{
    for (const auto& [name, renderPass] : m_renderPasses) {
        renderPass->construct(resourceManager);
    }
}

void GpuPipeline::addRenderPass(const std::string& name, const RenderPass::RenderPassConstructorFunction& constructorFunction)
{
    auto renderPass = std::make_unique<RenderPass>(constructorFunction);
    addRenderPass(name, std::move(renderPass));
}

void GpuPipeline::addRenderPass(const std::string& name, std::unique_ptr<RenderPass> renderPass)
{
    const auto& previousPass = m_renderPasses.find(name);
    if (previousPass != m_renderPasses.end()) {
        LogErrorAndExit("GpuPipeline::addRenderPass: called for pass with name '%s' but it aleady exist in this pipeline!\n", name.c_str());
    }

    m_renderPasses[name] = std::move(renderPass);
}
