#pragma once

#include "mathkit.h"
#include "rendering/Resources.h"

class Material {
public:
    std::string baseColor {};
    std::string normalMap {};
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

    virtual std::vector<uint16_t> indexData() const = 0;
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

    virtual const Mesh* operator[](size_t index) const = 0;
    virtual void forEachMesh(std::function<void(const Mesh&)>) const = 0;

private:
    Transform m_transform {};
};

class Scene {
public:
    Scene(std::initializer_list<Model*> models)
        : m_models(models)
    {
    }

    [[nodiscard]] size_t modelCount() const
    {
        return m_models.size();
    }

    const Model* operator[](size_t index) const
    {
        if (index > modelCount()) {
            return nullptr;
        }
        return m_models[index];
    }

private:
    std::vector<Model*> m_models;
};
