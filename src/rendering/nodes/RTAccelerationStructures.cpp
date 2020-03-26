#include "RTAccelerationStructures.h"

RTAccelerationStructures::RTAccelerationStructures(const Scene& scene)
    : RenderGraphNode(RTAccelerationStructures::name())
    , m_scene(scene)
{
}

std::string RTAccelerationStructures::name()
{
    return "rt-acceleration-structures";
}

void RTAccelerationStructures::constructNode(Registry& nodeReg)
{
    m_instances.clear();

    m_scene.forEachModel([&](size_t, const Model& model) {
        model.forEachMesh([&](const Mesh& mesh) {
            RTGeometry geometry { .vertexBuffer = nodeReg.createBuffer(mesh.positionData(), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal),
                                  .vertexFormat = VertexFormat::XYZ32F,
                                  .vertexStride = sizeof(vec3),
                                  .indexBuffer = nodeReg.createBuffer(mesh.indexData(), Buffer::Usage::Index, Buffer::MemoryHint::GpuOptimal),
                                  .indexType = mesh.indexType(),
                                  .transform = mesh.transform().localMatrix() };

            // TODO: Later we probably want to keep all meshes of a model in a single BLAS, but that requires some fancy SBT stuff which I don't wanna mess with now.
            BottomLevelAS& blas = nodeReg.createBottomLevelAccelerationStructure({ geometry });
            m_instances.push_back({ .blas = blas,
                                    .transform = model.transform() });
        });
    });
}

RenderGraphNode::ExecuteCallback RTAccelerationStructures::constructFrame(Registry& reg) const
{
    TopLevelAS& tlas = reg.createTopLevelAccelerationStructure(m_instances);
    reg.publish("scene", tlas);

    return [&](const AppState& appState, CommandList& cmdList) {
        cmdList.rebuildTopLevelAcceratationStructure(tlas);
    };
}
