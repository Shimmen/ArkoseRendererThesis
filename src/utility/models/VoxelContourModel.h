#pragma once

#include "utility/Model.h"
#include <vector>

class VoxelContourModel final : public Model {
public:
    struct VoxelContour {
        aabb3 aabb;
        vec3 normal;
        float distance;
        uint32_t colorIndex;
    };

    VoxelContourModel(std::vector<VoxelContour>, std::vector<vec3> colors);
    VoxelContourModel() = default;
    ~VoxelContourModel() = default;

    bool hasMeshes() const override;
    void forEachMesh(std::function<void(const Mesh&)>) const override;

    const std::vector<VoxelContour>& contours() const { return m_contours; }
    const std::vector<vec3>& colors() const { return m_colors; }

private:
    std::vector<VoxelContour> m_contours;
    std::vector<vec3> m_colors;
};
