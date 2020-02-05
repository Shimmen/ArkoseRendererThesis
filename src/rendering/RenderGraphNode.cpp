#include "RenderGraphNode.h"
#include <utility/logging.h>

RenderGraphNode::RenderGraphNode(NodeConstructorFunction function)
    : m_constructor_function(std::move(function))
    , m_command_submission_callbacks()
{
}

void RenderGraphNode::setFrameMultiplicity(size_t frameMultiplicity)
{
    m_command_submission_callbacks.clear();
    m_command_submission_callbacks.resize(frameMultiplicity);
    m_frameMultiplicity = frameMultiplicity;
}

void RenderGraphNode::constructForFrame(Registry& registry, uint32_t frame)
{
    ASSERT(m_frameMultiplicity > 0);
    ASSERT(frame >= 0 && frame < m_frameMultiplicity);
    m_command_submission_callbacks[frame] = m_constructor_function(registry);
}

void RenderGraphNode::executeForFrame(const AppState& appState, CommandList& commandList, uint32_t frame) const
{
    ASSERT(m_frameMultiplicity > 0);
    ASSERT(frame >= 0 && frame < m_frameMultiplicity);
    m_command_submission_callbacks[frame](appState, commandList);
}
