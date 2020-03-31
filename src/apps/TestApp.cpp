#include "TestApp.h"

#include "rendering/nodes/FinalPostFxNode.h"
#include "rendering/nodes/ForwardRenderNode.h"
#include "rendering/nodes/RTAccelerationStructures.h"
#include "rendering/nodes/RTDiffuseGINode.h"
#include "rendering/nodes/RTFirstHitNode.h"
#include "rendering/nodes/RTReflectionsNode.h"
#include "rendering/nodes/SceneUniformNode.h"
#include "rendering/nodes/ShadowMapNode.h"
#include "rendering/nodes/SlowForwardRenderNode.h"
#include "utility/GlobalState.h"
#include "utility/Input.h"
#include "utility/models/GltfModel.h"
#include "utility/models/SphereSetModel.h"
#include <imgui.h>

void TestApp::setup(RenderGraph& graph)
{
    //m_scene = Scene::loadFromFile("assets/Scenes/test.json");
    m_scene = Scene::loadFromFile("assets/Scenes/proxy-test.json");
    //m_scene = Scene::loadFromFile("assets/Scenes/sponza.json");

    graph.addNode<SceneUniformNode>(*m_scene);
    graph.addNode<ShadowMapNode>(*m_scene);
    graph.addNode<RTAccelerationStructures>(*m_scene);
    //graph.addNode<RTFirstHitNode>(*m_scene);
    graph.addNode<SlowForwardRenderNode>(*m_scene);
    //graph.addNode<RTReflectionsNode>(*m_scene);
    graph.addNode<RTDiffuseGINode>(*m_scene);
    graph.addNode<FinalPostFxNode>(*m_scene);
}

void TestApp::update(float elapsedTime, float deltaTime)
{
    static bool showMetrics = false;

    ImGui::BeginMainMenuBar();
    if (ImGui::BeginMenu("Tools")) {
        ImGui::MenuItem("Show metrics", nullptr, &showMetrics);
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();

    if (showMetrics) {
        ImGui::Begin("Metrics");
        float ms = deltaTime * 1000.0f;
        ImGui::Text("Frame time: %.3f ms/frame", ms);
        ImGui::End();
    }

    ImGui::Begin("TestApp");
    ImGui::ColorEdit3("Sun color", value_ptr(m_scene->sun().color));
    ImGui::SliderFloat("Sun intensity", &m_scene->sun().intensity, 0.0f, 50.0f);
    ImGui::SliderFloat("Environment", &m_scene->environmentMultiplier(), 0.0f, 5.0f);
    if (ImGui::CollapsingHeader("Cameras")) {
        m_scene->cameraGui();
    }
    ImGui::End();

    const Input& input = Input::instance();
    m_scene->camera().update(input, GlobalState::get().windowExtent(), deltaTime);

    if (!m_spinningObject) {
        /*
        m_scene->forEachModel([&](size_t, const Model& model) {
            if (model.name() == "bunny") {
                m_spinningObject = &const_cast<Model&>(model);
            }
        });
        */
    } else {
        mat4 matrix = mathkit::translate(1.4f, 2.4f, 0.8f) * mathkit::axisAngleMatrix({ 0, 1, 0 }, elapsedTime * 3.1415f / 2.0f) * mathkit::scale(8);
        m_spinningObject->transform().setLocalMatrix(matrix);
    }

    float st = 0.4f * elapsedTime;
    vec3 lightPosition = vec3(cosf(st), 2.0f, sinf(st));
    //m_scene.sun().direction = -normalize(lightPosition);
}
