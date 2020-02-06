#include "TestApp.h"

#include "rendering/nodes/CameraUniformNode.h"
#include "rendering/nodes/FinalPostFxNode.h"
#include "rendering/nodes/ForwardRenderNode.h"
#include "rendering/nodes/ShadowMapNode.h"
#include "rendering/nodes/SlowForwardRenderNode.h"
#include "utility/GlobalState.h"
#include "utility/GltfModel.h"
#include "utility/Input.h"
#include <imgui.h>

void TestApp::setup(ResourceManager& staticResources, RenderGraph& graph)
{
    // Here we can do stuff like CPU work and GPU stuff that is fully or mostly static,
    // e.g. load textures, load meshes, set vertex buffers.

    m_cube = GltfModel::load("assets/Cube/Cube.gltf");
    m_cube->transform().setLocalMatrix(
        mathkit::translate(0, -1.6f, 0) *
        mathkit::axisAngleMatrix({0,1,0}, 0.3f) *
        mathkit::scale(3, 1, 3));

    m_boomBox = GltfModel::load("assets/BoomBox/BoomBoxWithAxes.gltf");

    m_scene = { m_boomBox.get(), m_cube.get() };

    m_scene.camera().lookAt({ 0, 1, 6 }, { 0, 0.5f, 0 });

    m_scene.sun().color = { 1, 1, 1 };
    m_scene.sun().intensity = 1.0f;
    m_scene.sun().direction = normalize(vec3(-1.0f, -1.0f, 0.0f));
    m_scene.sun().shadowMapSize = { 2048, 2048 };
    m_scene.sun().worldExtent = 6.0f;

    // TODO: It would be nice if there was a way to do this without specifying the name if you just want the default anyway..
    graph.addNode(CameraUniformNode::name(), CameraUniformNode::construct(m_scene.camera()));
    graph.addNode(ShadowMapNode::name(), ShadowMapNode::construct(m_scene));
    //graph.addNode(ForwardRenderNode::name(), ForwardRenderNode::construct(m_scene));
    graph.addNode(SlowForwardRenderNode::name(), SlowForwardRenderNode::construct(m_scene));
    graph.addNode(FinalPostFxNode::name(), FinalPostFxNode::construct());
}

void TestApp::update(float elapsedTime, float deltaTime)
{
    ImGui::ShowDemoWindow();

    const Input& input = Input::instance();
    m_scene.camera().update(input, GlobalState::get().windowExtent(), deltaTime);

    mat4 matrix = mathkit::axisAngleMatrix({ 0, 1, 0 }, elapsedTime * 3.1415f / 2.0f) * mathkit::scale(75);
    m_boomBox->transform().setLocalMatrix(matrix);
}
