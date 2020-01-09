#pragma once

#include "ApplicationState.h"
#include "Commands.h"
#include "ResourceManager.h"
#include "Resources.h"
#include "utility/copying.h"
#include <functional>
#include <memory>
#include <string>

// TODO: Rename to something like 'Node' or 'RenderNode' (but I guess it shouldn't just be for rendering (also GPGPU) so maybe a bad name?
class RenderPass : public Resource {
public:
    // FIXME: We probably want a command list type that the RenderPasses can't read from, but only add to!
    using CommandList = std::vector<std::unique_ptr<FrontendCommand>>;

    using NeedsConstructCallback = std::function<bool(const ApplicationState&)>;
    using CommandSubmissionCallback = std::function<void(const ApplicationState&, CommandList&)>;
    using RenderPassConstructorFunction = std::function<CommandSubmissionCallback(ResourceManager&)>;

    explicit RenderPass(RenderPassConstructorFunction);
    ~RenderPass() = default;
    //NON_COPYABLE(RenderPass)

    [[nodiscard]] const RenderTarget& target() const;
    [[nodiscard]] const std::vector<FrontendCommand*>& commands() const { return m_commands; }

    //! Constructs the pass with a resource manager that manages the resources for it.
    void construct(ResourceManager&);

    //! Executes the pass and returns the commands that need to be performed.
    void execute(const ApplicationState&, CommandList&);

    //! Returns true if the render pass needs to be (re)constructed with new resources, e.g. in case of a window resize.
    [[nodiscard]] bool needsConstruction(const ApplicationState&) const;

    //! Set the NeedsConstructCallback that is called every frame to override default reconstruction policy.
    void setNeedsConstructionCallback(NeedsConstructCallback);

protected:
    //! Call this function to regenerate the render pass resources.
    RenderPassConstructorFunction m_constructor_function {};

    //! Submits the rendering commands that the pass should perform.
    CommandSubmissionCallback m_command_submission_callback {};

    //! Given the application state for the current frame, this callback returns whether the pass needs to be reconstructed.
    NeedsConstructCallback m_needs_construct_callback {};

    //! The target that this pass renders to.
    std::optional<RenderTarget> m_target {};

    //! The commands that this render pass last submitted.
    std::vector<FrontendCommand*> m_commands {};
};
