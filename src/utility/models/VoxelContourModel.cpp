#include "VoxelContourModel.h"

VoxelContourModel::VoxelContourModel(std::vector<VoxelContour> contours)
    : m_contours(std::move(contours))
{
}

bool VoxelContourModel::hasMeshes() const
{
    return false;
}

void VoxelContourModel::forEachMesh(std::function<void(const Mesh&)>) const
{
}
