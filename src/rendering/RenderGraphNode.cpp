#include "RenderGraphNode.h"
#include <utility/logging.h>

RenderGraphNode::RenderGraphNode(NodeConstructorFunction function)
    : m_constructor_function(std::move(function))
    , m_command_submission_callbacks()
{
    m_command_submission_callbacks.reserve(3);
}

void RenderGraphNode::construct(ResourceManager& resourceManager, const ApplicationState& appState)
{
    if (m_command_submission_callbacks.size() == 3) {
        m_command_submission_callbacks.clear();
    }

    //auto exec_func = m_constructor_function(resourceManager, appState);
    ASSERT(appState.frameIndex() == m_command_submission_callbacks.size());
    m_command_submission_callbacks.push_back(m_constructor_function(resourceManager, appState));
}

void RenderGraphNode::execute(const ApplicationState& appState, CommandList& commandList, FrameAllocator& frameAllocator) const
{
    uint32_t imageIndex = appState.frameIndex() % 3;
    ASSERT(imageIndex < m_command_submission_callbacks.size());
    m_command_submission_callbacks[imageIndex](appState, commandList, frameAllocator);

    //m_command_submission_callback(appState, commandList, frameAllocator);
}
