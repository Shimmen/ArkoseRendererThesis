#pragma once

#include "AppState.h"
#include "CommandList.h"
#include "Commands.h"
#include "ResourceManager.h"
#include "Resources.h"
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
