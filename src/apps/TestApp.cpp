#include "TestApp.h"

#include "rendering/nodes/FinalPostFxNode.h"
#include "rendering/nodes/ForwardRenderNode.h"
#include "rendering/nodes/RTAccelerationStructures.h"
#include "rendering/nodes/RTAmbientOcclusion.h"
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
    m_scene = Scene::loadFromFile("assets/Scenes/eval/bunny_test.json");
    m_scene->camera().setMaxSpeed(5.0f);

    bool rtxOn = true;
    bool firstHit = true;

    graph.addNode<SceneUniformNode>(*m_scene);
    graph.addNode<ShadowMapNode>(*m_scene);
    graph.addNode<SlowForwardRenderNode>(*m_scene);
    if (rtxOn) {
        graph.addNode<RTAccelerationStructures>(*m_scene);
        graph.addNode<RTAmbientOcclusion>(*m_scene);
        graph.addNode<RTDiffuseGINode>(*m_scene);
        if (firstHit) {
            graph.addNode<RTFirstHitNode>(*m_scene);
        }
    }
    graph.addNode<FinalPostFxNode>(*m_scene);

    if (true) {
        ShadowMapSpec spotShadow { { 4096, 4096 }, "spot" };
        vec3 spotPosition = { 0.0f, 4.0f, 12.0f };
        vec3 spotTarget = { 0.0f, 3.0f, 0.0f };
        vec3 spotDirection = normalize(spotTarget - spotPosition);
        SpotLight spotLight { { 1, 1, 1 }, 1200.0f, spotShadow, spotPosition, spotDirection, mathkit::PI / 6.0f };
        m_scene->spotLights().push_back(spotLight);
    }
}

void TestApp::update(float elapsedTime, float deltaTime)
{
    ImGui::Begin("TestApp");
    ImGui::ColorEdit3("Sun color", value_ptr(m_scene->sun().color));
    ImGui::SliderFloat("Sun intensity", &m_scene->sun().intensity, 0.0f, 50.0f);
    if (!m_scene->spotLights().empty())
        ImGui::SliderFloat("Spot intensity", &m_scene->spotLights().front().intensity, 0.0f, 3000.0f);
    ImGui::SliderFloat("Environment", &m_scene->environmentMultiplier(), 0.0f, 5.0f);
    if (ImGui::CollapsingHeader("Cameras")) {
        m_scene->cameraGui();
    }
    ImGui::End();

    ImGui::Begin("Metrics");
    float ms = deltaTime * 1000.0f;
    ImGui::Text("Frame time: %.3f ms/frame", ms);
    ImGui::End();

    const Input& input = Input::instance();
    m_scene->camera().update(input, GlobalState::get().windowExtent(), deltaTime);

    if (!m_spinningObject) {
        m_scene->forEachModel([&](size_t, const Model& model) {
            if (model.name() == "barrel") {
                m_spinningObject = &const_cast<Model&>(model);
            }
        });
    } else {
        mat4 matrix = mathkit::translate(0, 2.0f + sinf(elapsedTime), 0)
            * mathkit::axisAngleMatrix({ 0, 1, 0 }, elapsedTime * 3.1415f / 2.0f)
            * mathkit::scale(6.0f + 1.0f * cosf(elapsedTime));
        m_spinningObject->transform().setLocalMatrix(matrix);
    }
}
