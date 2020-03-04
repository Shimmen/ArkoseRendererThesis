#include "TestApp.h"

#include "rendering/nodes/FinalPostFxNode.h"
#include "rendering/nodes/ForwardRenderNode.h"
#include "rendering/nodes/RTDiffuseGINode.h"
#include "rendering/nodes/RTFirstHitNode.h"
#include "rendering/nodes/RTReflectionsNode.h"
#include "rendering/nodes/SceneUniformNode.h"
#include "rendering/nodes/ShadowMapNode.h"
#include "rendering/nodes/SlowForwardRenderNode.h"
#include "utility/GlobalState.h"
#include "utility/GltfModel.h"
#include "utility/Input.h"
#include <imgui.h>

void TestApp::setup(RenderGraph& graph)
{
    m_cornellBox = GltfModel::load("assets/CornellBox/CornellBoxFilled.gltf");
    m_cornellBox->transform().setLocalMatrix(mathkit::translate(-6, 0, 0) * mathkit::axisAngleMatrix({ 0, 1, 0 }, 0.3f) * mathkit::scale(3, 3, 3));

    m_palletStack = GltfModel::load("assets/PicaPicaPalletStack/scene.gltf");
    m_palletStack->transform().setLocalMatrix(mathkit::translate(7, 0, 0) * mathkit::axisAngleMatrix(mathkit::globalUp, -0.5) * mathkit::scale(0.3, 0.3, 0.3));

    //m_boomBox = GltfModel::load("assets/BoomBox/BoomBoxWithAxes.gltf");
    //m_boomBox->transform().setLocalMatrix(mathkit::translate(25, 0, 5));

    m_scene = { m_cornellBox.get(), m_palletStack.get(), /*m_boomBox.get()*/ };

    m_scene.camera().lookAt({ 0, 6, 20 }, { 0, 3, 0 });

    m_scene.setEnvironmentMap("assets/environments/tiergarten_2k.hdr");
    m_scene.environmentMultiplier() = 1.7f;

    m_scene.sun().color = { 1, 1, 1 };
    m_scene.sun().intensity = 30.0f;
    m_scene.sun().direction = normalize(vec3(1.0f, -0.3f, -1.0f));
    m_scene.sun().shadowMapSize = { 2048, 2048};
    m_scene.sun().worldExtent = 18.0f;

    graph.addNode<SceneUniformNode>(m_scene);
    graph.addNode<ShadowMapNode>(m_scene);
    //graph.addNode<RTFirstHitNode>(m_scene);
    graph.addNode<SlowForwardRenderNode>(m_scene);
    //graph.addNode<RTReflectionsNode>(m_scene);
    graph.addNode<RTDiffuseGINode>(m_scene);
    graph.addNode<FinalPostFxNode>(m_scene);
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
    ImGui::ColorEdit3("Sun color", value_ptr(m_scene.sun().color));
    ImGui::SliderFloat("Sun intensity", &m_scene.sun().intensity, 0.0f, 50.0f);
    ImGui::SliderFloat("Environment", &m_scene.environmentMultiplier(), 0.0f, 5.0f);
    ImGui::End();

    const Input& input = Input::instance();
    m_scene.camera().update(input, GlobalState::get().windowExtent(), deltaTime);

    if (m_boomBox) {
        mat4 matrix = mathkit::translate(1.4f, 2.4f, 0.8f) * mathkit::axisAngleMatrix({ 0, 1, 0 }, elapsedTime * 3.1415f / 2.0f) * mathkit::scale(50);
        m_boomBox->transform().setLocalMatrix(matrix);
    }

    float st = 0.4f * elapsedTime;
    vec3 lightPosition = vec3(cosf(st), 2.0f, sinf(st));
    //m_scene.sun().direction = -normalize(lightPosition);
}
