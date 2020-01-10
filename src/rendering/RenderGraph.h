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
    RenderGraph() = default;
    ~RenderGraph() = default;

    NON_COPYABLE(RenderGraph);
    RenderGraph(RenderGraph&&) noexcept = default;

    bool needsReconstruction(const ApplicationState&) const;
    void constructAll(ResourceManager&);

    void addNode(const std::string& name, const RenderGraphNode::NodeConstructorFunction&);
    void addNode(const std::string& name, std::unique_ptr<RenderGraphNode>);

    void forEachNodeInResolvedOrder(const std::function<void(const RenderGraphNode&)>&);

private:
    std::unordered_map<std::string, std::unique_ptr<RenderGraphNode>> m_nodes;
};
