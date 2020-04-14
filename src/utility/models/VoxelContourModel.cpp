#include "VoxelContourModel.h"

VoxelContourModel::VoxelContourModel(std::vector<VoxelContour> contours, std::vector<vec3> colors)
    : m_contours(std::move(contours))
    , m_colors(std::move(colors))
{
}

bool VoxelContourModel::hasMeshes() const
{
    return false;
}

void VoxelContourModel::forEachMesh(std::function<void(const Mesh&)>) const
{
}
