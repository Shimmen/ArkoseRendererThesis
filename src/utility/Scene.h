#pragma once

#include "Model.h"
#include "utility/mathkit.h"
#include <json.hpp>
#include <memory>
#include <optional>
#include <string>

struct ShadowMapSpec {
    Extent2D size;
    std::string name;
};

class Light {
public:
    Light() = default;
    Light(vec3 color, float intensity, std::optional<ShadowMapSpec> shadowMap)
        : color(color)
        , intensity(intensity)
        , shadowMap(shadowMap)
    {
    }

    virtual mat4 lightProjection() const = 0;

    vec3 color { 1, 1, 1 };
    float intensity { 1.0f };
    std::optional<ShadowMapSpec> shadowMap;
};

class SunLight : public Light {
public:
    SunLight() = default;
    SunLight(vec3 color, float intensity, std::optional<ShadowMapSpec> shadowMap, vec3 direction, float worldExtent)
        : Light(color, intensity, shadowMap)
        , direction(direction)
        , worldExtent(worldExtent)
    {
    }

    mat4 lightProjection() const override;

    vec3 direction { 0, 0, -1 };
    float worldExtent { 30.0f };
};

class SpotLight : public Light {
public:
    SpotLight() = default;
    SpotLight(vec3 color, float intensity, std::optional<ShadowMapSpec> shadowMap, vec3 position, vec3 direction, float coneAngle)
        : Light(color, intensity, shadowMap)
        , position(position)
        , direction(direction)
        , coneAngle(coneAngle)
    {
    }

    mat4 lightProjection() const override;

    vec3 position { 0, 0, 0 };
    vec3 direction { 0, 0, -1 };
    float coneAngle { mathkit::PI / 2.0f };
};

class Scene {
public:
    static constexpr const char* savedCamerasFile = "assets/cameras.json";
    static std::unique_ptr<Scene> loadFromFile(const std::string&);

    Scene(std::string);
    ~Scene();

    Model* addModel(std::unique_ptr<Model>);

    [[nodiscard]] size_t modelCount() const;
    const Model* operator[](size_t index) const;

    void forEachModel(std::function<void(size_t, const Model&)> callback) const;
    int forEachDrawable(std::function<void(int, const Mesh&)> callback) const;

    void cameraGui();
    const FpsCamera& camera() const { return m_currentMainCamera; }
    FpsCamera& camera() { return m_currentMainCamera; }

    const SunLight& sun() const { return m_sunLight; }
    SunLight& sun() { return m_sunLight; }

    const std::vector<SpotLight>& spotLights() const { return m_spotLights; }
    std::vector<SpotLight>& spotLights() { return m_spotLights; }

    void forEachLight(std::function<void(const Light&)>) const;

    void setEnvironmentMap(std::string path) { m_environmentMap = std::move(path); }
    const std::string& environmentMap() const { return m_environmentMap; }

    float environmentMultiplier() const { return m_environmentMultiplier; }
    float& environmentMultiplier() { return m_environmentMultiplier; }

private:
    void loadAdditionalCameras();

    static std::unique_ptr<Model> loadProxy(const std::string&);
    static std::unique_ptr<Model> loadSphereSetProxy(const nlohmann::json&);
    static std::unique_ptr<Model> loadVoxelContourProxy(const nlohmann::json&);

private:
    std::string m_loadedPath {};

    std::vector<std::unique_ptr<Model>> m_models;

    SunLight m_sunLight;
    std::vector<SpotLight> m_spotLights;

    FpsCamera m_currentMainCamera;
    std::unordered_map<std::string, FpsCamera> m_allCameras {};

    std::string m_environmentMap {};
    float m_environmentMultiplier { 1.0f };
};
