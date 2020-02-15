#pragma once

#include "AppState.h"
#include "ResourceManager.h"
#include "Resources.h"
#include "rendering/CommandList.h"
#include "utility/ArenaAllocator.h"
#include "utility/copying.h"
#include <functional>
#include <memory>
#include <string>

class RenderGraphNode {
public:
    using CommandSubmissionCallback = std::function<void(const AppState&, CommandList&)>;
    using NodeConstructorFunction = std::function<CommandSubmissionCallback(Registry&)>;

    explicit RenderGraphNode(NodeConstructorFunction);
    ~RenderGraphNode() = default;

    NON_COPYABLE(RenderGraphNode)

    void setFrameMultiplicity(size_t frameMultiplicity);

    //! Constructs the node with a resource manager that manages the resources for it.
    void constructForFrame(Registry&, uint32_t frame);

    //! Executes the node and returns the commands that need to be performed.
    void executeForFrame(const AppState&, CommandList&, uint32_t frame) const;

private:
    //! Call this function to regenerate the node resources.
    NodeConstructorFunction m_constructor_function {};

    //! Submits the rendering commands that the node should perform.
    std::vector<CommandSubmissionCallback> m_command_submission_callbacks {};

    //! The number of swapchain images / frames that this node needs to "manage"
    uint32_t m_frameMultiplicity { 0 };
};

class NEWRenderGraphNode {
public:
    explicit NEWRenderGraphNode(std::string name);
    virtual ~NEWRenderGraphNode() = default;

    using ExecuteCallback = std::function<void(const AppState&, CommandList&)>;

    [[nodiscard]] const std::string& name() const;

    //! This is not const since we need to write to members here that are shared for the whole node.
    virtual void constructNode(ResourceManager&) = 0;

    //! This is const, since changing or writing to any members would probably break stuff
    //! since this is called n times, one for each frame at reconstruction.
    virtual ExecuteCallback constructFrame(ResourceManager&) const = 0;

private:
    std::string m_name;
};

class NEWBasicRenderGraphNode final : public NEWRenderGraphNode {
public:
    using ConstructorFunction = std::function<ExecuteCallback(ResourceManager&)>;
    NEWBasicRenderGraphNode(std::string name, ConstructorFunction);

    void constructNode(ResourceManager&) override;
    ExecuteCallback constructFrame(ResourceManager&) const override;

private:
    ConstructorFunction m_constructorFunction;
};