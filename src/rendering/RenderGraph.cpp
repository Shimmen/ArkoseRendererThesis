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

void NEWRenderGraph::addNode(const std::string& name, const NEWBasicRenderGraphNode::ConstructorFunction& constructorFunction)
{
    // All nodes should be added before construction!
    ASSERT(m_frameContexts.empty());

    auto node = std::make_unique<NEWBasicRenderGraphNode>(name, constructorFunction);
    m_allNodes.emplace_back(std::move(node));
}

void NEWRenderGraph::addNode(std::unique_ptr<NEWRenderGraphNode>&& node)
{
    m_allNodes.emplace_back(std::move(node));
}

void NEWRenderGraph::constructAll(ResourceManager& nodeManager, std::vector<ResourceManager*> frameManagers)
{
    m_frameContexts.clear();

    // TODO: For debugability it would be nice if the frame resources were constructed right after the node resources, for each node

    for (auto& node : m_allNodes) {
        nodeManager.setCurrentNode(node->name());
        node->constructNode(nodeManager);
    }

    for (auto& frameManager : frameManagers) {
        FrameContext frameCtx {};

        for (auto& node : m_allNodes) {
            frameManager->setCurrentNode(node->name());
            auto executeCallback = node->constructFrame(*frameManager);
            frameCtx.nodeContexts.push_back({ .node = node.get(),
                .executeCallback = executeCallback });
        }

        m_frameContexts[frameManager] = frameCtx;
    }

    nodeManager.setCurrentNode("-");
    for (auto& frameManager : frameManagers) {
        frameManager->setCurrentNode("-");
    }
}

void NEWRenderGraph::forEachNodeInResolvedOrder(const ResourceManager& frameManager, std::function<void(const NEWRenderGraphNode::ExecuteCallback&)> callback) const
{
    auto entry = m_frameContexts.find(&frameManager);
    ASSERT(entry != m_frameContexts.end());

    const FrameContext& frameContext = entry->second;
    for (auto& [_, execCallback] : frameContext.nodeContexts) {
        callback(execCallback);
    }
}
