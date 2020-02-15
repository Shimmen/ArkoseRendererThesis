#include "RenderGraphNode.h"
#include <utility/logging.h>

#include <utility>

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

NEWRenderGraphNode::NEWRenderGraphNode(std::string name)
    : m_name(std::move(name))
{
}

const std::string& NEWRenderGraphNode::name() const
{
    return m_name;
}

NEWBasicRenderGraphNode::NEWBasicRenderGraphNode(std::string name, ConstructorFunction constructorFunction)
    : NEWRenderGraphNode(std::move(name))
    , m_constructorFunction(std::move(constructorFunction))
{
}

void NEWBasicRenderGraphNode::constructNode(ResourceManager&)
{
    // Intentionally empty. If you want to have node resource, create a custom RenderGraphNode subclass.
}

NEWBasicRenderGraphNode::ExecuteCallback NEWBasicRenderGraphNode::constructFrame(ResourceManager& frameManager) const
{
    return m_constructorFunction(frameManager);
}
