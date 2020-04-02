#pragma once

#include "utility/Model.h"
#include <vector>

class VoxelContourModel final : public Model {
public:
    struct VoxelContour {
        aabb3 aabb;
        vec3 normal;
        float centerOffset;
    };

    explicit VoxelContourModel(std::vector<VoxelContour>);
    VoxelContourModel() = default;
    ~VoxelContourModel() = default;

    bool hasMeshes() const override;
    void forEachMesh(std::function<void(const Mesh&)>) const override;

    const std::vector<VoxelContour>& contours() const { return m_contours; }

private:
    std::vector<VoxelContour> m_contours;
};
