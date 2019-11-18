#include "RenderPass.h"

#include <utility>

RenderPass::RenderPass(RenderPassConstructorFunction function)
    : m_constructor_function(std::move(function))
    , m_command_submission_callback(nullptr)
{
}

RenderToScreenPass::RenderToScreenPass(RenderPassConstructorFunction function)
    : RenderPass(std::move(function))
{
}

RenderToScreenPass::~RenderToScreenPass()
{
    // TODO! Do stuff here?!
}

bool RenderToScreenPass::needsConstruction(const ApplicationState& appState) const
{
    if (m_needs_construct_callback) {
        TargetState targetState {
            .extent = appState.windowExtent,
            .didChange = appState.windowSizeDidChange
        };
        return m_needs_construct_callback(appState, targetState);
    } else {
        // FIXME: The isFirstFrame should probably be automatic.. Unless we want some type of lazy init? But it's kind of error prone..
        return appState.isFirstFrame || appState.windowSizeDidChange;
    }
}

RenderPassChangeRequest RenderToScreenPass::changeRequest(const ApplicationState& appState) const
{
    if (m_change_request_callback) {
        TargetState targetState {
            .extent = appState.windowExtent,
            .didChange = appState.windowSizeDidChange
        };
        return m_change_request_callback(appState, targetState);
    } else {
        return RenderPassChangeRequest::ResubmitCommands;
    }
}

/////////

RenderToFramebufferPass::RenderToFramebufferPass(Framebuffer&& target, RenderPassConstructorFunction function)
    : RenderPass(std::move(function))
    , m_target(std::move(target))
{
}

RenderToFramebufferPass::~RenderToFramebufferPass()
{
    // TODO! Do stuff here?!
}

bool RenderToFramebufferPass::needsConstruction(const ApplicationState& appState) const
{
    TargetState targetState {
        .extent = m_target.extent(),
        .didChange = false // TODO: How are we supposed to know this?!
    };

    if (m_needs_construct_callback) {
        return m_needs_construct_callback(appState, targetState);
    } else {
        return targetState.didChange;
    }
}

RenderPassChangeRequest RenderToFramebufferPass::changeRequest(const ApplicationState& appState) const
{
    if (m_change_request_callback) {
        TargetState targetState {
            .extent = m_target.extent(),
            .didChange = false // TODO: How are we supposed to know this?!
        };
        return m_change_request_callback(appState, targetState);
    } else {
        return RenderPassChangeRequest::ResubmitCommands;
    }
}
