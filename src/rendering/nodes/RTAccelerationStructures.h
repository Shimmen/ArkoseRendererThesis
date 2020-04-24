#pragma once

#include "../RenderGraphNode.h"
#include "utility/Scene.h"

class SphereSetModel;
class VoxelContourModel;

class RTAccelerationStructures final : public RenderGraphNode {
public:
    explicit RTAccelerationStructures(const Scene&);
    ~RTAccelerationStructures() override = default;

    std::optional<std::string> displayName() const override { return "RT Acceleration Structures"; }

    static std::string name();

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

    // This isn't a perfect solution, because if we only need spheres then we would like
    // to place that hit group at index 0 so we don't waste space with unused shaders etc.
    enum HitGroupIndex : uint32_t {
        Triangle = 0,
        Sphere = 1,
        VoxelContour = 2,
    };

    enum HitMask : uint8_t {
        TriangleMeshWithoutProxy = 0x01,
        TriangleMeshWithProxy = 0x02,
        SphereSetHitMask = 0x04,
        VoxelContourHitMask = 0x08,
    };

private:
    RTGeometry createGeometryForTriangleMesh(const Mesh&, Registry&) const;
    RTGeometry createGeometryForSphereSet(const SphereSetModel&, Registry&) const;
    RTGeometry createGeometryForVoxelContours(const VoxelContourModel&, Registry&) const;

    RTGeometryInstance createGeometryInstance(const RTGeometry&, const Transform&, uint32_t customId, uint8_t hitMask, uint32_t sbtOffset, Registry&) const;

private:
    const Scene& m_scene;

    std::vector<RTGeometryInstance> m_mainInstances {};
    std::vector<RTGeometryInstance> m_proxyInstances {};
};
