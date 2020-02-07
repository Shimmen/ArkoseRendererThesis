#include "RenderGraph.h"

#include <utility/logging.h>

RenderGraph::RenderGraph(size_t frameMultiplicity)
    : m_frameMultiplicity(frameMultiplicity)
{
}

void RenderGraph::constructAllForFrame(Registry& registry, uint32_t frame)
{
    for (const auto& [name, node] : m_nodes) {
        //registry.node.setCurrentNode(name);
        registry.frame.setCurrentNode(name);
        node->constructForFrame(registry, frame);
    }
}

void RenderGraph::addNode(const std::string& name, const RenderGraphNode::NodeConstructorFunction& constructorFunction)
{
    auto renderPass = std::make_unique<RenderGraphNode>(constructorFunction);
    addNode(name, std::move(renderPass));
}

void RenderGraph::addNode(const std::string& name, std::unique_ptr<RenderGraphNode> node)
{
    const auto& previousNode = m_nodeIndexFromName.find(name);
    if (previousNode != m_nodeIndexFromName.end()) {
        LogErrorAndExit("GpuPipeline::addNode: called for node with name '%s' but it already exist in this graph!\n", name.c_str());
    }

    node->setFrameMultiplicity(m_frameMultiplicity);
    m_nodeIndexFromName[name] = m_nodes.size();
    m_nodes.emplace_back(name, std::move(node));
}

void RenderGraph::forEachNodeInResolvedOrder(const ResourceManager& associatedResourceManager, const std::function<void(const RenderGraphNode&)>& callback) const
{
    // TODO: We also have to make sure that nodes rendering to the screen are last (and in some respective order that makes sense)
    //auto& dependencies = associatedResourceManager.nodeDependencies();

    // TODO: Actually run the callback in the correctly resolved order!
    // TODO: Actually run the callback in the correctly resolved order!
    // TODO: Actually run the callback in the correctly resolved order!
    // TODO: Actually run the callback in the correctly resolved order!

    for (auto& [_, node] : m_nodes) {
        callback(*node);
    }
}
