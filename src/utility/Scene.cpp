#include "Scene.h"

#include "FileIO.h"
#include "GltfModel.h"
#include "Logging.h"
#include <fstream>
#include <imgui.h>
#include <json.hpp>

mat4 SunLight::lightProjection() const
{
    mat4 lightOrientation = mathkit::lookAt({ 0, 0, 0 }, normalize(direction)); // (note: currently just centered on the origin)
    mat4 lightProjection = mathkit::orthographicProjection(worldExtent, 1.0f, -worldExtent, worldExtent);
    return lightProjection * lightOrientation;
}

Scene Scene::loadFromFile(const std::string& path)
{
    using json = nlohmann::json;

    if (!FileIO::isFileReadable(path)) {
        LogErrorAndExit("Could not read scene file '%s', exiting\n", path.c_str());
    }

    json jsonScene;
    std::ifstream fileStream(path);
    fileStream >> jsonScene;

    Scene scene;

    auto jsonEnv = jsonScene.at("environment");
    jsonEnv.at("texture").get_to(scene.m_environmentMap);
    jsonEnv.at("multiplier").get_to(scene.m_environmentMultiplier);

    for (auto& jsonModel : jsonScene.at("models")) {
        std::string modelGltf;
        jsonModel.at("gltf").get_to(modelGltf);
        Model* model = scene.addModel(GltfModel::load(modelGltf));

        auto transform = jsonModel.at("transform");

        std::vector<float> translation;
        transform.at("translation").get_to(translation);

        std::vector<float> scale;
        transform.at("scale").get_to(scale);

        mat4 rotationMatrix;
        auto jsonRotation = transform.at("rotation");
        std::string rotType = jsonRotation.at("type");
        if (rotType == "axis-angle") {
            std::vector<float> axis;
            jsonRotation.at("axis").get_to(axis);
            float angle;
            jsonRotation.at("angle").get_to(angle);
            rotationMatrix = mathkit::axisAngleMatrix({ axis[0], axis[1], axis[2] }, angle);
        }

        mat4 localMatrix = mathkit::translate(translation[0], translation[1], translation[2])
            * rotationMatrix * mathkit::scale(scale[0], scale[1], scale[2]);
        model->transform().setLocalMatrix(localMatrix);
    }

    for (auto& jsonLight : jsonScene.at("lights")) {
        ASSERT(jsonLight.at("type") == "directional");

        SunLight sun;

        float color[3];
        jsonLight.at("color").get_to(color);
        sun.color = { color[0], color[1], color[2] };

        jsonLight.at("intensity").get_to(sun.intensity);

        float dir[3];
        jsonLight.at("direction").get_to(dir);
        sun.direction = normalize(vec3(dir[0], dir[1], dir[2]));

        jsonLight.at("worldExtent").get_to(sun.worldExtent);

        int mapSize[2];
        jsonLight.at("shadowMapSize").get_to(mapSize);
        sun.shadowMapSize = { mapSize[0], mapSize[1] };

        // TODO!
        scene.m_sunLight = sun;
    }

    for (auto& jsonCamera : jsonScene.at("cameras")) {

        FpsCamera camera;

        std::string name = jsonCamera.at("name");

        float origin[3];
        jsonCamera.at("origin").get_to(origin);

        float target[3];
        jsonCamera.at("target").get_to(target);

        camera.lookAt({ origin[0], origin[1], origin[2] }, { target[0], target[1], target[2] }, mathkit::globalUp);
        scene.m_altCameras[name] = camera;
    }

    scene.m_currentMainCamera = scene.m_altCameras["main"];

    return scene;
}

Model* Scene::addModel(std::unique_ptr<Model> model)
{
    m_models.push_back(std::move(model));
    return m_models.back().get();
}

size_t Scene::modelCount() const
{
    return m_models.size();
}

const Model* Scene::operator[](size_t index) const
{
    if (index > modelCount()) {
        return nullptr;
    }
    return m_models[index].get();
}

void Scene::forEachModel(std::function<void(size_t, const Model&)> callback) const
{
    for (size_t i = 0; i < modelCount(); ++i) {
        const Model& model = *m_models[i];
        callback(i, model);
    }
}

int Scene::forEachDrawable(std::function<void(int, const Mesh&)> callback) const
{
    int nextIndex = 0;
    for (auto& model : m_models) {
        model->forEachMesh([&](const Mesh& mesh) {
            callback(nextIndex++, mesh);
        });
    }
    return nextIndex;
}

void Scene::cameraGui()
{
    for (const auto& [name, camera] : m_altCameras) {
        if (ImGui::Button(name.c_str())) {
            m_currentMainCamera = camera;
        }
    }

    ImGui::Separator();
    static char nameBuffer[63];
    ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer), ImGuiInputTextFlags_AutoSelectAll);
    if (ImGui::Button("Save current")) {
        m_altCameras[nameBuffer] = m_currentMainCamera;
    }
}
