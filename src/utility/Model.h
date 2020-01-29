#pragma once

#include "mathkit.h"
#include "rendering/Resources.h"

class Mesh {
public:
    Mesh() = default;
    virtual ~Mesh() = default;

    virtual std::vector<vec3> positionData() const = 0;
    virtual std::vector<vec2> texcoordData() const = 0;
    virtual std::vector<vec3> normalData() const = 0;
    virtual std::vector<vec4> tangentData() const = 0;

    virtual std::vector<uint16_t> indexData() const = 0;
    virtual size_t indexCount() const = 0;
    virtual bool isIndexed() const = 0;
};

class Model {
public:
    Model() = default;
    virtual ~Model() = default;

    virtual const Mesh* operator[](size_t index) const = 0;
    virtual void forEachMesh(std::function<void(const Mesh&)>) const = 0;
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
