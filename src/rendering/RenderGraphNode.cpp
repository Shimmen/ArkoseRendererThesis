#include "RenderGraphNode.h"
#include <utility/logging.h>

RenderGraphNode::RenderGraphNode(NodeConstructorFunction function)
    : m_constructor_function(std::move(function))
    , m_command_submission_callback(nullptr)
{
}

void RenderGraphNode::construct(ResourceManager& resourceManager, const ApplicationState& appState)
{
    m_command_submission_callback = m_constructor_function(resourceManager, appState);
}

void RenderGraphNode::execute(const ApplicationState& appState, CommandList& commandList)
{
    m_command_submission_callback(appState, commandList);
}
