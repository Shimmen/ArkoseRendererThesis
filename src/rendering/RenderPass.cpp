#include "RenderPass.h"
#include <utility/logging.h>

RenderPass::RenderPass(RenderPassConstructorFunction function)
    : m_constructor_function(std::move(function))
    , m_command_submission_callback(nullptr)
{
}

RenderPass::~RenderPass()
{
    // TODO! Do stuff here?!
}

const RenderTarget& RenderPass::target() const
{
    if (!m_target.has_value()) {
        LogErrorAndExit("Invalid RenderPass: no target set! Exiting.\n");
    }
    return m_target.value();
}

void RenderPass::construct(ResourceManager& resourceManager)
{
    m_command_submission_callback = m_constructor_function(resourceManager);
}

void RenderPass::execute(const ApplicationState& appState, CommandList& commandList)
{
    m_command_submission_callback(appState, commandList);
}

bool RenderPass::needsConstruction(const ApplicationState& appState) const
{
    if (m_needs_construct_callback) {
        return m_needs_construct_callback(appState);
    } else {
        // FIXME: The appState.frameIndex == 0 part should probably be automatic. It always needs to construct stuff before the first frame.
        return appState.frameIndex == 0 || appState.windowSizeDidChange;
    }
}

void RenderPass::setNeedsConstructionCallback(RenderPass::NeedsConstructCallback callback)
{
    m_needs_construct_callback = std::move(callback);
}

RenderPassChangeRequest RenderPass::changeRequest(const ApplicationState& appState) const
{
    if (m_change_request_callback) {
        return m_change_request_callback(appState);
    } else {
        return RenderPassChangeRequest::ResubmitCommands;
    }
}

void RenderPass::setChangeRequestCallback(RenderPass::ChangeRequestCallback callback)
{
    m_change_request_callback = std::move(callback);
}
