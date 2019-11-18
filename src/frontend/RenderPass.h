#pragma once

#include "Resources.h"
#include "utility/copying.h"
#include <functional>

struct ApplicationState {
    const bool isFirstFrame;
    const Extent2D windowExtent;
    const bool windowSizeDidChange;
};

struct TargetState {
    const Extent2D extent;
    const bool didChange; // TODO: How exactly should we keep track of this?!
};

class ResourceBuilder {
    // TODO: This is passed to the construct passes and is used to allocate
    //  resources (e.g. textures & buffers) used in the execute pass.
public:
    explicit ResourceBuilder(const ApplicationState& appState)
        : m_appState(appState)
    {
    }

    // TODO: Add a nice API for creating transient resources here

    // TODO: A possible idea is that there is a resource builder for each render pass,
    //  and when the pass is reconstructed the old resource builder will release any
    //  resources it is holding, as a means of resource lifetime management..?

private:
    const ApplicationState m_appState;
};

enum class RenderPassChangeRequest {
    NoChange,
    DoNotExecute,
    ResubmitCommands
};

class RenderPass {
public:
    using NeedsConstructCallback = std::function<bool(const ApplicationState&, const TargetState&)>;
    using ChangeRequestCallback = std::function<RenderPassChangeRequest(const ApplicationState&, const TargetState&)>;

    using CommandSubmissionCallback = std::function<void()>;
    using RenderPassConstructorFunction = std::function<CommandSubmissionCallback(const ResourceBuilder&)>;

    [[nodiscard]] virtual bool needsConstruction(const ApplicationState&) const = 0;
    [[nodiscard]] virtual RenderPassChangeRequest changeRequest(const ApplicationState&) const = 0;

protected:
    explicit RenderPass(RenderPassConstructorFunction);
    virtual ~RenderPass() = default;

    NON_COPYABLE(RenderPass)

    //! Call this function to regenerate
    RenderPassConstructorFunction m_constructor_function {};

    //! Submits the rendering commands that the pass should perform.
    CommandSubmissionCallback m_command_submission_callback {};

    //! Given the application state for the current frame, this callback returns whether the pass needs to be reconstructed.
    NeedsConstructCallback m_needs_construct_callback {};

    //! Queries the pass regarding changes to commands executed in the command submission callback.
    ChangeRequestCallback m_change_request_callback {};
};

class RenderToScreenPass : public RenderPass {
public:
    NON_COPYABLE(RenderToScreenPass)

    explicit RenderToScreenPass(RenderPassConstructorFunction);
    ~RenderToScreenPass() override;

    [[nodiscard]] bool needsConstruction(const ApplicationState&) const override;
    [[nodiscard]] RenderPassChangeRequest changeRequest(const ApplicationState&) const override;
};

class RenderToFramebufferPass : public RenderPass {
public:
    NON_COPYABLE(RenderToFramebufferPass)

    RenderToFramebufferPass(Framebuffer&&, RenderPassConstructorFunction);
    ~RenderToFramebufferPass() override;

    [[nodiscard]] bool needsConstruction(const ApplicationState&) const override;
    [[nodiscard]] RenderPassChangeRequest changeRequest(const ApplicationState&) const override;

private:
    Framebuffer m_target;
};
