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
    std::vector<RTGeometryInstance> instances {};

    for (size_t modelIdx = 0; modelIdx < m_scene.modelCount(); ++modelIdx) {
        const Model& model = *m_scene[modelIdx];

        std::vector<RTGeometry> geometries {};
        model.forEachMesh([&](const Mesh& mesh) {
            // TODO: Somehow include the mesh.transform().localMatrix() in the geometry data!
            geometries.push_back({ .vertexBuffer = nodeReg.createBuffer(mesh.positionData(), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal),
                                   .vertexFormat = VertexFormat::XYZ32F,
                                   .indexBuffer = nodeReg.createBuffer(mesh.indexData(), Buffer::Usage::Index, Buffer::MemoryHint::GpuOptimal),
                                   .indexType = IndexType::UInt16 });
        });

        BottomLevelAS& blas = nodeReg.createBottomLevelAccelerationStructure(geometries);
        instances.push_back({ .blas = blas,
                              .transform = model.transform().worldMatrix() });
    }

    m_tlas = &nodeReg.createTopLevelAccelerationStructure(instances);
}

RenderGraphNode::ExecuteCallback RTReflectionsNode::constructFrame(Registry&) const
{
    return [](const AppState& appState, CommandList& cmdList) {
        // todo!
    };
}
