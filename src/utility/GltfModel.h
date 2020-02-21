#pragma once

#include "Model.h"
#include <memory>
#include <string>
#include <tiny_gltf.h>

class GltfModel;

class GltfMesh : public Mesh {
public:
    explicit GltfMesh(std::string name, const GltfModel* parent, const tinygltf::Model&, const tinygltf::Primitive&, mat4 matrix);
    ~GltfMesh() = default;

    [[nodiscard]] Material material() const override;

    [[nodiscard]] std::vector<vec3> positionData() const override;
    [[nodiscard]] std::vector<vec2> texcoordData() const override;
    [[nodiscard]] std::vector<vec3> normalData() const override;
    [[nodiscard]] std::vector<vec4> tangentData() const override;

    [[nodiscard]] std::vector<uint16_t> indexData() const override;
    [[nodiscard]] size_t indexCount() const override;
    [[nodiscard]] bool isIndexed() const override;

    VertexFormat vertexFormat() const override;
    IndexType indexType() const override;

private:
    const tinygltf::Accessor& getAccessor(const char* name) const;

private:
    std::string m_name;
    const GltfModel* m_parentModel;
    const tinygltf::Model* m_model;
    const tinygltf::Primitive* m_primitive;
};

class GltfModel : public Model {
public:
    explicit GltfModel(std::string path, const tinygltf::Model&);
    GltfModel() = default;
    ~GltfModel() = default;

    [[nodiscard]] static std::unique_ptr<Model> load(const std::string& path);

    const Mesh* operator[](size_t index) const override;
    void forEachMesh(std::function<void(const Mesh&)>) const override;

    [[nodiscard]] std::string directory() const;

private:
    std::string m_path {};
    const tinygltf::Model* m_model {};
    std::vector<GltfMesh> m_meshes {};
};
