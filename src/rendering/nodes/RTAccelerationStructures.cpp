#include "RTAccelerationStructures.h"

#include "RTData.h"
#include "utility/models/SphereSetModel.h"
#include "utility/models/VoxelContourModel.h"

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

    uint32_t nextTriangleInstanceId = 0;
    uint32_t nextSphereInstanceId = 0;
    uint32_t nextVoxelContourInstanceId = 0;

    m_scene.forEachModel([&](size_t, const Model& model) {
        model.forEachMesh([&](const Mesh& mesh) {
            RTGeometry geometry = createGeometryForTriangleMesh(mesh, nodeReg);
            uint8_t hitMask = model.hasProxy() ? HitMask::TriangleMeshWithProxy : HitMask::TriangleMeshWithoutProxy;
            RTGeometryInstance instance = createGeometryInstance(geometry, model.transform(), nextTriangleInstanceId++, hitMask, HitGroupIndex::Triangle, nodeReg);
            m_mainInstances.push_back(instance);
        });

        if (model.proxy().hasMeshes()) {
            model.proxy().forEachMesh([&](const Mesh& proxyMesh) {
                RTGeometry proxyGeometry = createGeometryForTriangleMesh(proxyMesh, nodeReg);
                RTGeometryInstance instance = createGeometryInstance(proxyGeometry, model.transform(), nextTriangleInstanceId++, HitMask::TriangleMeshWithoutProxy, HitGroupIndex::Triangle, nodeReg);
                m_proxyInstances.push_back(instance);
            });
        } else {
            const auto* sphereSetModel = dynamic_cast<const SphereSetModel*>(&model.proxy());
            if (sphereSetModel) {
                RTGeometry sphereSetGeometry = createGeometryForSphereSet(*sphereSetModel, nodeReg);
                RTGeometryInstance instance = createGeometryInstance(sphereSetGeometry, model.transform(), nextSphereInstanceId++, HitMask::SphereSetHitMask, HitGroupIndex::Sphere, nodeReg);
                m_proxyInstances.push_back(instance);
                return;
            }

            const auto* voxelContourModel = dynamic_cast<const VoxelContourModel*>(&model.proxy());
            if (voxelContourModel) {
                RTGeometry voxelContourGeometry = createGeometryForVoxelContours(*voxelContourModel, nodeReg);
                RTGeometryInstance instance = createGeometryInstance(voxelContourGeometry, model.transform(), nextVoxelContourInstanceId++, HitMask::VoxelContourHitMask, HitGroupIndex::VoxelContour, nodeReg);
                m_proxyInstances.push_back(instance);
                return;
            }

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
    RTTriangleGeometry geometry { .vertexBuffer = reg.createBuffer(mesh.positionData(), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal),
                                  .vertexFormat = VertexFormat::XYZ32F,
                                  .vertexStride = sizeof(vec3),
                                  .indexBuffer = reg.createBuffer(mesh.indexData(), Buffer::Usage::Index, Buffer::MemoryHint::GpuOptimal),
                                  .indexType = mesh.indexType(),
                                  .transform = mesh.transform().localMatrix() };
    return geometry;
}

RTGeometry RTAccelerationStructures::createGeometryForSphereSet(const SphereSetModel& set, Registry& reg) const
{
    std::vector<RTAABB> aabbData;
    for (const auto& sphere : set.spheres()) {
        vec3 center = vec3(sphere);
        float radius = sphere.w;

        RTAABB sphereAabb;
        sphereAabb.min = center - vec3(radius);
        sphereAabb.max = center + vec3(radius);

        aabbData.push_back(sphereAabb);
    }

    constexpr size_t aabbStride = sizeof(RTAABB);
    static_assert(aabbStride % 8 == 0);

    RTAABBGeometry geometry { .aabbBuffer = reg.createBuffer(std::move(aabbData), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal),
                              .aabbStride = aabbStride };
    return geometry;
}

RTGeometry RTAccelerationStructures::createGeometryForVoxelContours(const VoxelContourModel& contourModel, Registry& reg) const
{
    std::vector<RTAABB> aabbData;
    for (const auto& contour : contourModel.contours()) {

        RTAABB aabb;
        aabb.min = contour.aabb.min;
        aabb.max = contour.aabb.max;

        aabbData.push_back(aabb);
    }

    constexpr size_t aabbStride = sizeof(RTAABB);
    static_assert(aabbStride % 8 == 0);

    RTAABBGeometry geometry { .aabbBuffer = reg.createBuffer(std::move(aabbData), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal),
                              .aabbStride = aabbStride };
    return geometry;
}

RTGeometryInstance RTAccelerationStructures::createGeometryInstance(const RTGeometry& geometry, const Transform& transform, uint32_t customId, uint8_t hitMask, uint32_t sbtOffset, Registry& reg) const
{
    // TODO: Later we probably want to keep all meshes of a model in a single BLAS, but that requires some fancy SBT stuff which I don't wanna mess with now.
    BottomLevelAS& blas = reg.createBottomLevelAccelerationStructure({ geometry });
    RTGeometryInstance instance = { .blas = blas,
                                    .transform = transform,
                                    .shaderBindingTableOffset = sbtOffset,
                                    .customInstanceId = customId,
                                    .hitMask = hitMask };
    return instance;
}
