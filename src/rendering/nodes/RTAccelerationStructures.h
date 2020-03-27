#pragma once

#include "../RenderGraphNode.h"
#include "utility/Scene.h"

class SphereSetModel;

class RTAccelerationStructures final : public RenderGraphNode {
public:
    explicit RTAccelerationStructures(const Scene&);
    ~RTAccelerationStructures() override = default;

    static std::string name();

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

private:
    RTGeometry createGeometryForTriangleMesh(const Mesh&, Registry&) const;
    RTGeometry createGeometryForSphereSet(const SphereSetModel&, Registry&) const;

    RTGeometryInstance createGeometryInstance(const RTGeometry&, const Transform&, Registry&) const;

private:
    const Scene& m_scene;

    std::vector<RTGeometryInstance> m_mainInstances {};
    std::vector<RTGeometryInstance> m_proxyInstances {};
};
