#pragma once

#include "Resources.h"
#include "utility/copying.h"
#include <functional>

struct ApplicationState {
    const int frameIndex;

    const double deltaTime;
    const double timeSinceStartup;

    const Extent2D windowExtent;
    const bool windowSizeDidChange;
};

class ResourceManager {
    // TODO: This is passed to the construct passes and is used to allocate
    //  resources (e.g. textures & buffers) used in the execute pass.
public:
    explicit ResourceManager(ApplicationState appState)
        : m_appState(std::move(appState))
    {
    }

    // TODO: Add a nice API for creating & managing resources here

    // TODO: Idea for implementation: remember all previous resources created from it (from previous frames)
    //  and if everything is the same, don't actually delete and construct new resources that are the same.
    //  Example situation: the window resizes, the render pass is reconstructed, and the code requests an
    //  identical buffer for e.g. static data, so we simply return the last used one. Maybe we then only delete
    //  resources when we ask for new stuff or when you manually request that things are release (e.g. at shutdown).
    //  Internally a backend could keep track of <RenderPass, ResourceManager> pairs which are in sync with each other!

    // TODO: Another idea for implementation! Since all other resources go through this resource builder
    //  (really it should be called ResourceManager) why not manage a "static" resource manager for an App object,
    //  which manages all resources that persist throughout the whole application lifetime.

    [[nodiscard]] RenderTarget getWindowRenderTarget();
    [[nodiscard]] RenderTarget createRenderTarget(std::initializer_list<RenderTarget::Attachment>);

    [[nodiscard]] Texture2D createTexture2D(int width, int height);
    [[nodiscard]] Buffer createBuffer(size_t size);

    void setBufferDataImmediately(Buffer, void* data, size_t size, size_t offset = 0);

private:
    const ApplicationState m_appState;
    // TODO: Add some type of references to resources here so it can keep track of stuff.
};

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

    using CommandSubmissionCallback = std::function<void()>;
    using RenderPassConstructorFunction = std::function<CommandSubmissionCallback(ResourceManager&)>;

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
