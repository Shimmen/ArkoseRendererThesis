#include "SphereSetModel.h"

SphereSetModel::SphereSetModel(std::vector<Sphere> spheres, std::vector<SphericalHarmonics> sphericalHarmonics)
    : m_spheres(std::move(spheres))
    , m_sphericalHarmonics(std::move(sphericalHarmonics))
{
    ASSERT(m_spheres.size() == m_sphericalHarmonics.size());
}

bool SphereSetModel::hasMeshes() const
{
    return false;
}

void SphereSetModel::forEachMesh(std::function<void(const Mesh&)>) const
{
}
