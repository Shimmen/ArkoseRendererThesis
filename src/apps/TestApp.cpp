#include "TestApp.h"

#include "rendering/nodes/CameraUniformNode.h"
#include "rendering/nodes/FinalPostFxNode.h"
#include "rendering/nodes/ForwardRenderNode.h"
#include "rendering/nodes/RTReflectionsNode.h"
#include "rendering/nodes/ShadowMapNode.h"
#include "rendering/nodes/SlowForwardRenderNode.h"
#include "utility/GlobalState.h"
#include "utility/GltfModel.h"
#include "utility/Input.h"
#include <imgui.h>

void TestApp::setup(RenderGraph& graph)
{
    m_testRoom = GltfModel::load("assets/CornellBox/CornellBox.gltf");
    m_testRoom->transform().setLocalMatrix(mathkit::axisAngleMatrix({ 0, 1, 0 }, 0.3f) * mathkit::scale(3, 3, 3));

    m_boomBox = GltfModel::load("assets/BoomBox/BoomBoxWithAxes.gltf");

    m_scene = { m_boomBox.get(), m_testRoom.get() };

    m_scene.camera().lookAt({ 0, 1, 6 }, { 0, 0.5f, 0 });

    m_scene.sun().color = { 1, 1, 1 };
    m_scene.sun().intensity = 10.0f;
    m_scene.sun().direction = normalize(vec3(-1.0f, -1.0f, 0.0f));
    m_scene.sun().shadowMapSize = { 2048, 2048 };
    m_scene.sun().worldExtent = 6.0f;

    graph.addNode<CameraUniformNode>(m_scene.camera());
    graph.addNode<ShadowMapNode>(m_scene);
    graph.addNode<RTReflectionsNode>(m_scene);
    graph.addNode<SlowForwardRenderNode>(m_scene);
    graph.addNode<FinalPostFxNode>();
}

void TestApp::update(float elapsedTime, float deltaTime)
{
    ImGui::Begin("TestApp");
    {
        ImGui::ColorEdit3("Sun color", value_ptr(m_scene.sun().color));
        ImGui::SliderFloat("Sun intensity", &m_scene.sun().intensity, 0.0f, 20.0f);
    }
    ImGui::End();

    const Input& input = Input::instance();
    m_scene.camera().update(input, GlobalState::get().windowExtent(), deltaTime);

    mat4 matrix = mathkit::translate(1.4f, 2.2f, 0.8f) * mathkit::axisAngleMatrix({ 0, 1, 0 }, elapsedTime * 3.1415f / 2.0f) * mathkit::scale(50);
    m_boomBox->transform().setLocalMatrix(matrix);
}
