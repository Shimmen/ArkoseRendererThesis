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
    m_mainInstances.clear();
    m_proxyInstances.clear();

    m_scene.forEachModel([&](size_t, const Model& model) {
        model.forEachMesh([&](const Mesh& mesh) {
            RTGeometry geometry = createGeometryForTriangleMesh(mesh, nodeReg);
            RTGeometryInstance instance = createGeometryInstance(geometry, model.transform(), nodeReg);
            m_mainInstances.push_back(instance);
        });

        if (model.proxy().hasMeshes()) {
            model.proxy().forEachMesh([&](const Mesh& proxyMesh) {
                RTGeometry proxyGeometry = createGeometryForTriangleMesh(proxyMesh, nodeReg);
                RTGeometryInstance instance = createGeometryInstance(proxyGeometry, model.transform(), nodeReg);
                m_proxyInstances.push_back(instance);
            });
        } else {
            // TODO: Handle other types of proxies here!
            ASSERT_NOT_REACHED();
        }
    });
}

RenderGraphNode::ExecuteCallback RTAccelerationStructures::constructFrame(Registry& reg) const
{
    TopLevelAS& main = reg.createTopLevelAccelerationStructure(m_mainInstances);
    reg.publish("scene", main);

    TopLevelAS& proxy = reg.createTopLevelAccelerationStructure(m_proxyInstances);
    reg.publish("proxy", proxy);

    return [&](const AppState& appState, CommandList& cmdList) {
        cmdList.rebuildTopLevelAcceratationStructure(main);
        cmdList.rebuildTopLevelAcceratationStructure(proxy);
    };
}

RTGeometry RTAccelerationStructures::createGeometryForTriangleMesh(const Mesh& mesh, Registry& reg) const
{
    RTGeometry geometry { .vertexBuffer = reg.createBuffer(mesh.positionData(), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal),
                          .vertexFormat = VertexFormat::XYZ32F,
                          .vertexStride = sizeof(vec3),
                          .indexBuffer = reg.createBuffer(mesh.indexData(), Buffer::Usage::Index, Buffer::MemoryHint::GpuOptimal),
                          .indexType = mesh.indexType(),
                          .transform = mesh.transform().localMatrix() };
    return geometry;
}

RTGeometryInstance RTAccelerationStructures::createGeometryInstance(const RTGeometry& geometry, const Transform& transform, Registry& reg) const
{
    // TODO: Later we probably want to keep all meshes of a model in a single BLAS, but that requires some fancy SBT stuff which I don't wanna mess with now.
    BottomLevelAS& blas = reg.createBottomLevelAccelerationStructure({ geometry });
    RTGeometryInstance instance = { .blas = blas,
                                    .transform = transform };
    return instance;
}
