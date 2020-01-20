#pragma once

#include "ApplicationState.h"
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

    void constructAllForFrame(ResourceManager&, uint32_t frame);

    void addNode(const std::string& name, const RenderGraphNode::NodeConstructorFunction&);
    void addNode(const std::string& name, std::unique_ptr<RenderGraphNode>);

    void forEachNodeInResolvedOrder(const std::function<void(const RenderGraphNode&)>&) const;

private:
    std::unordered_map<std::string, std::unique_ptr<RenderGraphNode>> m_nodes;

    //! The number of swapchain images / frames that this node needs to "manage"
    uint32_t m_frameMultiplicity;
};
