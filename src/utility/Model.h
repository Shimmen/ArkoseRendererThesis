#pragma once

#include "utility/FpsCamera.h"
#include "utility/mathkit.h"
#include <functional>

class Material {
public:
    std::string baseColor {};
    vec4 baseColorFactor { 1.0f };

    std::string normalMap {};
    std::string metallicRoughness {};
    std::string emissive {};
};

class Transform {
public:
    explicit Transform(mat4 localMatrix = mat4(1.0f), const Transform* parent = nullptr)
        : m_parent(parent)
        , m_localMatrix(localMatrix)
    {
    }

    void setLocalMatrix(mat4 matrix)
    {
        m_localMatrix = matrix;
    }

    mat4 localMatrix() const
    {
        return m_localMatrix;
    }

    mat4 worldMatrix() const
    {
        if (!m_parent) {
            return m_localMatrix;
        }
        return m_parent->worldMatrix() * m_localMatrix;
    }

    mat3 normalMatrix() const
    {
        mat3 world3x3 = mat3(worldMatrix());
        mat3 normalMatrix = transpose(inverse(world3x3));
        return normalMatrix;
    }

    // ..
    //Transform& setScale(float);
    //Transform& rotateBy(float);
private:
    //vec3 m_translation { 0.0 };
    //quat m_orientation {};
    //vec3 m_scale { 1.0 };
    const Transform* m_parent {};
    mutable mat4 m_localMatrix { 1.0f };
};

enum class VertexFormat {
    XYZ32F
};

enum class IndexType {
    UInt16,
    UInt32,
};

class Mesh {
public:
    Mesh(Transform transform)
        : m_transform(transform)
    {
    }
    virtual ~Mesh() = default;

    virtual const Transform& transform() const { return m_transform; }

    virtual Material material() const = 0;

    virtual std::vector<vec3> positionData() const = 0;
    virtual std::vector<vec2> texcoordData() const = 0;
    virtual std::vector<vec3> normalData() const = 0;
    virtual std::vector<vec4> tangentData() const = 0;

    virtual VertexFormat vertexFormat() const = 0;
    virtual IndexType indexType() const = 0;

    virtual std::vector<uint32_t> indexData() const = 0;
    virtual size_t indexCount() const = 0;
    virtual bool isIndexed() const = 0;

private:
    Transform m_transform {};
};

class Model {
public:
    Model() = default;
    virtual ~Model() = default;

    Transform& transform() { return m_transform; }
    const Transform& transform() const { return m_transform; }

    virtual void forEachMesh(std::function<void(const Mesh&)>) const = 0;

    bool hasProxy() { return m_proxy != nullptr; }
    void setProxy(std::unique_ptr<Model> proxy) { m_proxy = std::move(proxy); }
    const Model& proxy() { return *m_proxy; }

private:
    Transform m_transform {};
    std::unique_ptr<Model> m_proxy {};
};
