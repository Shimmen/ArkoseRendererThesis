#pragma once

#include "ApplicationState.h"
#include "CommandList.h"
#include "Commands.h"
#include "ResourceManager.h"
#include "Resources.h"
#include "utility/copying.h"
#include "utility/FrameAllocator.h"
#include <functional>
#include <memory>
#include <string>

class RenderGraphNode : public Resource {
public:
    using CommandSubmissionCallback = std::function<void(const ApplicationState&, CommandList&, FrameAllocator&)>;
    using NodeConstructorFunction = std::function<CommandSubmissionCallback(ResourceManager&, const ApplicationState&)>;

    explicit RenderGraphNode(NodeConstructorFunction);
    ~RenderGraphNode() = default;

    NON_COPYABLE(RenderGraphNode)

    //! Constructs the node with a resource manager that manages the resources for it.
    void construct(ResourceManager&, const ApplicationState&);

    //! Executes the node and returns the commands that need to be performed.
    void execute(const ApplicationState&, CommandList&, FrameAllocator&) const;

protected:
    //! Call this function to regenerate the node resources.
    NodeConstructorFunction m_constructor_function {};

    //! Submits the rendering commands that the node should perform.
    CommandSubmissionCallback m_command_submission_callback {};
};
