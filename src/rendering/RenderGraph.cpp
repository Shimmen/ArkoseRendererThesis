#include "RenderGraph.h"

#include <utility/logging.h>

bool RenderGraph::needsReconstruction(const ApplicationState& appState) const
{
    for (const auto& [name, renderPass] : m_nodes) {
        if (renderPass->needsConstruction(appState)) {
            return true;
        }
    }

    return false;
}

void RenderGraph::constructAll(ResourceManager& resourceManager)
{
    for (const auto& [name, node] : m_nodes) {
        resourceManager.setCurrentPass(name);
        node->construct(resourceManager);
    }
}

void RenderGraph::addNode(const std::string& name, const RenderGraphNode::NodeConstructorFunction& constructorFunction)
{
    auto renderPass = std::make_unique<RenderGraphNode>(constructorFunction);
    addNode(name, std::move(renderPass));
}

void RenderGraph::addNode(const std::string& name, std::unique_ptr<RenderGraphNode> node)
{
    const auto& previousNode = m_nodes.find(name);
    if (previousNode != m_nodes.end()) {
        LogErrorAndExit("GpuPipeline::addNode: called for node with name '%s' but it already exist in this graph!\n", name.c_str());
    }

    m_nodes[name] = std::move(node);
}

void RenderGraph::forEachNodeInResolvedOrder(const std::function<void(const RenderGraphNode&)>& callback)
{
    // TODO: This is obviously a temporary limitation, hehe..
    ASSERT(m_nodes.size() == 1);

    for (auto& [name, node] : m_nodes) {
        callback(*node);
    }
}
