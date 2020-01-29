#include "GltfModel.h"

#include "utility/logging.h"
#include <string>
#include <unordered_map>

static std::unordered_map<std::string, tinygltf::Model> s_loadedModels {};

std::unique_ptr<Model> GltfModel::load(const std::string& path)
{
    auto entry = s_loadedModels.find(path);
    if (entry != s_loadedModels.end()) {
        tinygltf::Model& internal = s_loadedModels[path];
        return std::make_unique<GltfModel>(path, internal);
    }

    tinygltf::TinyGLTF loader {};
    tinygltf::Model& internal = s_loadedModels[path];

    std::string error;
    std::string warning;

    // TODO: Check if ASCII or binary!
    bool result = loader.LoadASCIIFromFile(&internal, &error, &warning, path);

    if (!warning.empty()) {
        LogWarning("glTF loader warning: %s\n", warning.c_str());
    }

    if (!error.empty()) {
        LogError("glTF loader error: %s\n", error.c_str());
    }

    if (!result) {
        LogError("glTF loader: could not load file '%s'\n", path.c_str());
        return nullptr;
    }

    if (internal.defaultScene == -1 && internal.scenes.size() > 1) {
        LogWarning("glTF loader: scene ambiguity in model '%s'\n", path.c_str());
    }

    return std::make_unique<GltfModel>(path, internal);
}

GltfModel::GltfModel(std::string path, const tinygltf::Model& model)
    : m_path(std::move(path))
    , m_model(&model)
{
    const tinygltf::Scene& scene = (m_model->defaultScene != -1)
        ? m_model->scenes[m_model->defaultScene]
        : m_model->scenes.front();

    std::function<void(const tinygltf::Node&, mat4)> findMeshesRecursively = [&](const tinygltf::Node& node, mat4 matrix) {
        mat4 nodeMatrix = node.matrix.empty() ? mat4(1.0f) : mathkit::linearToMat4(node.matrix);
        matrix = nodeMatrix * matrix;

        if (node.mesh != -1) {
            auto& mesh = m_model->meshes[node.mesh];
            for (int i = 0; i < mesh.primitives.size(); ++i) {

                std::string meshName = mesh.name;
                if (mesh.primitives.size() > 1) {
                    meshName += "_" + std::to_string(i);
                }

                m_meshes.emplace_back(meshName, model, mesh.primitives[i], matrix);
            }
        }

        for (int childIdx : node.children) {
            auto& child = model.nodes[childIdx];
            findMeshesRecursively(child, matrix);
        }
    };

    for (int nodeIdx : scene.nodes) {
        auto& node = model.nodes[nodeIdx];
        findMeshesRecursively(node, mat4(1.0f));
    }
}

const Mesh* GltfModel::operator[](size_t index) const
{
    if (index >= m_meshes.size()) {
        return nullptr;
    }

    return &m_meshes[index];
}

void GltfModel::forEachMesh(std::function<void(const Mesh&)> callback) const
{
    for (const Mesh& mesh : m_meshes) {
        callback(mesh);
    }
}

GltfMesh::GltfMesh(std::string name, const tinygltf::Model& model, const tinygltf::Primitive& primitive, mat4 matrix)
    : m_name(std::move(name))
    , m_localMatrix(matrix)
    , m_model(&model)
    , m_primitive(&primitive)
{
    if (primitive.mode != TINYGLTF_MODE_TRIANGLES) {
        LogErrorAndExit("glTF mesh: primitive with mode other than triangles is not yet supported\n");
    }

    // TODO: Add bounding boxes so we can do sorting, etc.
    //meshInfo.aabb = mathkit::aabb(position.minValues, position.maxValues);
}

const tinygltf::Accessor& GltfMesh::getAccessor(const char* name) const
{
    auto entry = m_primitive->attributes.find(name);
    if (entry == m_primitive->attributes.end()) {
        LogErrorAndExit("glTF mesh: primitive is missing attribute of name '%s'\n", name);
    }
    return m_model->accessors[entry->second];
}

std::vector<vec3> GltfMesh::positionData() const
{
    const tinygltf::Accessor& accessor = getAccessor("POSITION");
    ASSERT(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
    ASSERT(accessor.type == TINYGLTF_TYPE_VEC3);

    const tinygltf::BufferView& view = m_model->bufferViews[accessor.bufferView];
    ASSERT(view.byteStride == 0); // (i.e. tightly packed)

    const tinygltf::Buffer& buffer = m_model->buffers[view.buffer];

    const unsigned char* start = buffer.data.data() + view.byteOffset;
    auto* first = reinterpret_cast<const vec3*>(start);

    // TODO: Cache this maybe?
    std::vector<vec3> vec { first, first + accessor.count };
    return vec;
}

std::vector<vec2> GltfMesh::texcoordData() const
{
    const tinygltf::Accessor& accessor = getAccessor("TEXCOORD_0");
    ASSERT(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
    ASSERT(accessor.type == TINYGLTF_TYPE_VEC2);

    const tinygltf::BufferView& view = m_model->bufferViews[accessor.bufferView];
    ASSERT(view.byteStride == 0); // (i.e. tightly packed)

    const tinygltf::Buffer& buffer = m_model->buffers[view.buffer];

    const unsigned char* start = buffer.data.data() + view.byteOffset;
    auto* first = reinterpret_cast<const vec2*>(start);

    // TODO: Cache this maybe?
    std::vector<vec2> vec { first, first + accessor.count };
    return vec;
}

std::vector<vec3> GltfMesh::normalData() const
{
    const tinygltf::Accessor& accessor = getAccessor("NORMAL");
    ASSERT(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
    ASSERT(accessor.type == TINYGLTF_TYPE_VEC3);

    const tinygltf::BufferView& view = m_model->bufferViews[accessor.bufferView];
    ASSERT(view.byteStride == 0); // (i.e. tightly packed)

    const tinygltf::Buffer& buffer = m_model->buffers[view.buffer];

    const unsigned char* start = buffer.data.data() + view.byteOffset;
    auto* first = reinterpret_cast<const vec3*>(start);

    // TODO: Cache this maybe?
    std::vector<vec3> vec { first, first + accessor.count };
    return vec;
}

std::vector<vec4> GltfMesh::tangentData() const
{
    const tinygltf::Accessor& accessor = getAccessor("TANGENT");
    ASSERT(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
    ASSERT(accessor.type == TINYGLTF_TYPE_VEC4);

    const tinygltf::BufferView& view = m_model->bufferViews[accessor.bufferView];
    ASSERT(view.byteStride == 0); // (i.e. tightly packed)

    const tinygltf::Buffer& buffer = m_model->buffers[view.buffer];

    const unsigned char* start = buffer.data.data() + view.byteOffset;
    auto* first = reinterpret_cast<const vec4*>(start);

    // TODO: Cache this maybe?
    std::vector<vec4> vec { first, first + accessor.count };
    return vec;
}

std::vector<uint16_t> GltfMesh::indexData() const
{
    ASSERT(isIndexed());
    const tinygltf::Accessor& accessor = m_model->accessors[m_primitive->indices];
    ASSERT(accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT);
    ASSERT(accessor.type == TINYGLTF_TYPE_SCALAR);

    const tinygltf::BufferView& view = m_model->bufferViews[accessor.bufferView];
    ASSERT(view.byteStride == 0); // (i.e. tightly packed)

    const tinygltf::Buffer& buffer = m_model->buffers[view.buffer];

    const unsigned char* start = buffer.data.data() + view.byteOffset;
    auto* first = reinterpret_cast<const uint16_t*>(start);

    // TODO: Cache this maybe?
    std::vector<uint16_t> vec { first, first + accessor.count };
    return vec;
}

size_t GltfMesh::indexCount() const
{
    ASSERT(isIndexed());
    const tinygltf::Accessor& accessor = m_model->accessors[m_primitive->indices];
    ASSERT(accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT);
    ASSERT(accessor.type == TINYGLTF_TYPE_SCALAR);

    return accessor.count;
}

bool GltfMesh::isIndexed() const
{
    return m_primitive->indices != -1;
}
