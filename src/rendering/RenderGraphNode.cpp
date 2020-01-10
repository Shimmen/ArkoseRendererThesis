#include "RenderGraphNode.h"
#include <utility/logging.h>

RenderGraphNode::RenderGraphNode(NodeConstructorFunction function)
    : m_constructor_function(std::move(function))
    , m_command_submission_callback(nullptr)
{
}

void RenderGraphNode::construct(ResourceManager& resourceManager)
{
    m_command_submission_callback = m_constructor_function(resourceManager);
}

void RenderGraphNode::execute(const ApplicationState& appState, CommandList& commandList)
{
    m_command_submission_callback(appState, commandList);
}

bool RenderGraphNode::needsConstruction(const ApplicationState& appState) const
{
    if (m_needs_construct_callback) {
        return m_needs_construct_callback(appState);
    } else {
        // FIXME: The appState.frameIndex == 0 part should probably be automatic. It always needs to construct stuff before the first frame.
        return appState.frameIndex == 0 || appState.windowSizeDidChange;
    }
}

void RenderGraphNode::setNeedsConstructionCallback(RenderGraphNode::NeedsConstructCallback callback)
{
    m_needs_construct_callback = std::move(callback);
}
