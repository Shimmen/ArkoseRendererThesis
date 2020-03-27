#include "SphereSetModel.h"

SphereSetModel::SphereSetModel(std::vector<Sphere> spheres)
    : m_spheres(std::move(spheres))
{
}

bool SphereSetModel::hasMeshes() const
{
    return false;
}

void SphereSetModel::forEachMesh(std::function<void(const Mesh&)>) const
{
}
