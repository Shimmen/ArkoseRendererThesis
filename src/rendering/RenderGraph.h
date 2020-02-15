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
    RenderGraph() = default;
    ~RenderGraph() = default;

    RenderGraph(RenderGraph&) = delete;
    RenderGraph& operator=(RenderGraph&) = delete;

    void addNode(const std::string& name, const RenderGraphBasicNode::ConstructorFunction&);
    void addNode(std::unique_ptr<RenderGraphNode>&&);

    //! Construct all nodes & set up a per-frame context for each resource manager frameManagers
    void constructAll(ResourceManager& nodeManager, std::vector<ResourceManager*> frameManagers);

    //! The callback is called for each node (in correct order). The provided resource manager is used to map to the
    void forEachNodeInResolvedOrder(const ResourceManager&, std::function<void(const RenderGraphNode::ExecuteCallback&)>) const;

private:
    struct NodeContext {
        RenderGraphNode* node;
        RenderGraphNode::ExecuteCallback executeCallback;
    };
    struct FrameContext {
        std::vector<NodeContext> nodeContexts {};
    };

    //! All nodes that are part of this graph
    std::vector<std::unique_ptr<RenderGraphNode>> m_allNodes {};

    //! The frame contexts, one per frame (i.e. image in the swapchain)
    std::unordered_map<const ResourceManager*, FrameContext> m_frameContexts {};
};
