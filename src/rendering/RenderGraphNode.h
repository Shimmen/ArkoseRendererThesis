#pragma once

#include "ApplicationState.h"
#include "CommandList.h"
#include "Commands.h"
#include "ResourceManager.h"
#include "Resources.h"
#include "utility/copying.h"
#include <functional>
#include <memory>
#include <string>

class RenderGraphNode : public Resource {
public:
    using NeedsConstructCallback = std::function<bool(const ApplicationState&)>;
    using CommandSubmissionCallback = std::function<void(const ApplicationState&, CommandList&)>;
    using NodeConstructorFunction = std::function<CommandSubmissionCallback(ResourceManager&)>;

    explicit RenderGraphNode(NodeConstructorFunction);
    ~RenderGraphNode() = default;

    NON_COPYABLE(RenderGraphNode)

    //! Constructs the node with a resource manager that manages the resources for it.
    void construct(ResourceManager&);

    //! Executes the node and returns the commands that need to be performed.
    void execute(const ApplicationState&, CommandList&);

    //! Returns true if the node needs to be (re)constructed with new resources, e.g. in case of a window resize.
    [[nodiscard]] bool needsConstruction(const ApplicationState&) const;

    //! Set the NeedsConstructCallback that is called every frame to override default reconstruction policy.
    void setNeedsConstructionCallback(NeedsConstructCallback);

protected:
    //! Call this function to regenerate the node resources.
    NodeConstructorFunction m_constructor_function {};

    //! Submits the rendering commands that the node should perform.
    CommandSubmissionCallback m_command_submission_callback {};

    //! Given the application state for the current frame, this callback returns whether the node needs to be reconstructed.
    NeedsConstructCallback m_needs_construct_callback {};

    //! The target that this pass renders to.
    //std::optional<RenderTarget> m_target {};

    //! The commands that this render pass last submitted.
    //std::vector<FrontendCommand*> m_commands {};
};
