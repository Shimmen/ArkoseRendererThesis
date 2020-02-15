#pragma once

#include "AppState.h"
#include "RenderGraphNode.h"
#include "ResourceManager.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

class RenderGraph {
public:
    explicit RenderGraph(size_t frameMultiplicity);
    ~RenderGraph() = default;

    NON_COPYABLE(RenderGraph);
    RenderGraph(RenderGraph&&) noexcept = default;

    void constructAllForFrame(Registry&, uint32_t frame);

    void addNode(const std::string& name, const RenderGraphNode::NodeConstructorFunction&);
    void addNode(const std::string& name, std::unique_ptr<RenderGraphNode>);

    void forEachNodeInResolvedOrder(const ResourceManager&, const std::function<void(const RenderGraphNode&)>&) const;

private:
    std::vector<std::pair<std::string, std::unique_ptr<RenderGraphNode>>> m_nodes;
    std::unordered_map<std::string, size_t> m_nodeIndexFromName;

    //! The number of swapchain images / frames that this node needs to "manage"
    uint32_t m_frameMultiplicity;
};

class NEWRenderGraph {
public:
    NEWRenderGraph() = default;
    ~NEWRenderGraph() = default;

    NEWRenderGraph(NEWRenderGraph&) = delete;
    NEWRenderGraph& operator=(NEWRenderGraph&) = delete;

    void addNode(const std::string& name, const NEWBasicRenderGraphNode::ConstructorFunction&);
    void addNode(std::unique_ptr<NEWRenderGraphNode>&&);

    //! Construct all nodes & set up a per-frame context for each resource manager frameManagers
    void constructAll(ResourceManager& nodeManager, std::vector<ResourceManager*> frameManagers);

    //! The callback is called for each node (in correct order). The provided resource manager is used to map to the
    void forEachNodeInResolvedOrder(const ResourceManager&, std::function<void(const NEWRenderGraphNode::ExecuteCallback&)>) const;

private:
    struct NodeContext {
        NEWRenderGraphNode* node;
        NEWRenderGraphNode::ExecuteCallback executeCallback;
    };
    struct FrameContext {
        std::vector<NodeContext> nodeContexts {};
    };

    //! All nodes that are part of this graph
    std::vector<std::unique_ptr<NEWRenderGraphNode>> m_allNodes {};

    //! The frame contexts, one per frame (i.e. image in the swapchain)
    std::unordered_map<const ResourceManager*, FrameContext> m_frameContexts {};
};
