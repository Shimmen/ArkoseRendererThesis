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

    void constructAllForFrame(ResourceManager&, uint32_t frame);

    void addNode(const std::string& name, const RenderGraphNode::NodeConstructorFunction&);
    void addNode(const std::string& name, std::unique_ptr<RenderGraphNode>);

    void forEachNodeInResolvedOrder(const ResourceManager&, const std::function<void(const RenderGraphNode&)>&) const;

private:
    std::vector<std::pair<std::string, std::unique_ptr<RenderGraphNode>>> m_nodes;
    std::unordered_map<std::string, size_t> m_nodeIndexFromName;

    //! The number of swapchain images / frames that this node needs to "manage"
    uint32_t m_frameMultiplicity;
};
