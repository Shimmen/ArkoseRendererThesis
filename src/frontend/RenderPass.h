#pragma once

#include "ApplicationState.h"
#include "CommandSubmitter.h"
#include "ResourceManager.h"
#include "Resources.h"
#include "utility/copying.h"
#include <functional>

enum class RenderPassChangeRequest {
    //! The exact same command as previous frame should be submitted.
    NoChange,

    //! The commands of this pass should not be submitted this frame.
    DoNotExecute,

    //! New commands need to be submitted, so the CommandSubmissionCallback should be called this frame.
    ResubmitCommands
};

class RenderPass {
public:
    using NeedsConstructCallback = std::function<bool(const ApplicationState&)>;
    using ChangeRequestCallback = std::function<RenderPassChangeRequest(const ApplicationState&)>;

    using CommandSubmissionCallback = std::function<void(CommandSubmitter&)>;
    using RenderPassConstructorFunction = std::function<CommandSubmissionCallback(ResourceManager&)>;

    RenderPass() = default; // TODO: Should we have such a thing use instead just use null for members?
    explicit RenderPass(RenderPassConstructorFunction);
    ~RenderPass();

    //! Returns true if the render pass needs to be (re)constructed with new resources, e.g. in case of a window resize.
    [[nodiscard]] bool needsConstruction(const ApplicationState&) const;

    //! Set the NeedsConstructCallback that is called every frame to override default reconstruction policy.
    void setNeedsConstructionCallback(NeedsConstructCallback);

    //! Query the render pass if there are going to be changes to the commands submitted.
    [[nodiscard]] RenderPassChangeRequest changeRequest(const ApplicationState&) const;

    //! Set the ChangeRequestCallback that is called every frame to override default command submission policy.
    void setChangeRequestCallback(ChangeRequestCallback);

protected:
    NON_COPYABLE(RenderPass)

    //! Call this function to regenerate the render pass resources.
    RenderPassConstructorFunction m_constructor_function {};

    //! Submits the rendering commands that the pass should perform.
    CommandSubmissionCallback m_command_submission_callback {};

    //! Given the application state for the current frame, this callback returns whether the pass needs to be reconstructed.
    NeedsConstructCallback m_needs_construct_callback {};

    //! Queries the pass regarding changes to commands executed in the command submission callback.
    ChangeRequestCallback m_change_request_callback {};
};