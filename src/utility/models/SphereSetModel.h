#pragma once

#include "utility/Model.h"
#include "utility/mathkit.h"
#include <vector>

#include "SphericalHarmonics.h"

class SphereSetModel final : public Model {
public:
    using Sphere = vec4;

    SphereSetModel(std::vector<Sphere>, std::vector<SphericalHarmonics>);
    SphereSetModel() = default;
    ~SphereSetModel() = default;

    bool hasMeshes() const override;
    void forEachMesh(std::function<void(const Mesh&)>) const override;

    const std::vector<Sphere>& spheres() const { return m_spheres; }
    const std::vector<SphericalHarmonics>& sphericalHarmonics() const { return m_sphericalHarmonics; }

private:
    std::vector<Sphere> m_spheres;
    std::vector<SphericalHarmonics> m_sphericalHarmonics;
};
