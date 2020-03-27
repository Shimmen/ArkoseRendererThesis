#pragma once

#include "utility/Model.h"
#include <vector>

class SphereSetModel final : public Model {
public:
    using Sphere = vec4;

    explicit SphereSetModel(std::vector<Sphere>);
    SphereSetModel() = default;
    ~SphereSetModel() = default;

    bool hasMeshes() const override;
    void forEachMesh(std::function<void(const Mesh&)>) const override;

    const std::vector<Sphere>& spheres() const { return m_spheres; }

private:
    std::vector<Sphere> m_spheres;
};
