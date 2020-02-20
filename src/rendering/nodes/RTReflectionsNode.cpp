#include "RTReflectionsNode.h"

RTReflectionsNode::RTReflectionsNode(const Scene& scene)
    : RenderGraphNode(RTReflectionsNode::name())
    , m_scene(scene)
{
}

std::string RTReflectionsNode::name()
{
    return "rt-reflections";
}

void RTReflectionsNode::constructNode(Registry& nodeReg)
{
    m_scene.forEachDrawable([&](int, const Mesh& mesh) {

        Buffer& vertexBuffer = nodeReg.createBuffer(mesh.positionData(), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);
        Buffer& indexBuffer = nodeReg.createBuffer(mesh.indexData(), Buffer::Usage::Index, Buffer::MemoryHint::GpuOptimal);
        BottomLevelAS& blas = nodeReg.createBottomLevelAccelerationStructure(vertexBuffer, indexBuffer, VertexFormat::XYZ32F, IndexType::UInt16);

    });
}

RenderGraphNode::ExecuteCallback RTReflectionsNode::constructFrame(Registry&) const
{
    return [](const AppState& appState, CommandList& cmdList) {
        // todo!
    };
}
