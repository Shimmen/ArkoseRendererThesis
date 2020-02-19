#include "RTXReflectionsNode.h"

RTXReflectionsNode::RTXReflectionsNode(const Scene& scene)
    : RenderGraphNode(RTXReflectionsNode::name())
    , m_scene(scene)
{

}

std::string RTXReflectionsNode::name()
{
    return "rtx-reflections";
}

void RTXReflectionsNode::constructNode(Registry&)
{
}

RenderGraphNode::ExecuteCallback RTXReflectionsNode::constructFrame(Registry&) const
{
    return ExecuteCallback();
}
