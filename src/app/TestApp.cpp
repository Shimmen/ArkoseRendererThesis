#include "TestApp.h"

#include "rendering/nodes/CameraUniformNode.h"
#include "rendering/nodes/FinalPostFxNode.h"
#include "rendering/nodes/ForwardRenderNode.h"
#include "utility/GlobalState.h"
#include "utility/GltfModel.h"
#include "utility/Input.h"
#include <imgui.h>

void TestApp::setup(StaticResourceManager& staticResources, RenderGraph& graph)
{
    // Here we can do stuff like CPU work and GPU stuff that is fully or mostly static,
    // e.g. load textures, load meshes, set vertex buffers.

    m_camera.lookAt({ 0, 1, 3 }, { 0, 0.5f, 0 });

    m_model = GltfModel::load("assets/BoomBox/BoomBoxWithAxes.gltf");
    m_scene = { m_model.get() };

    // TODO: It would be nice if there was a way to do this without specifying the name if you just want the default anyway..
    graph.addNode(CameraUniformNode::name(), CameraUniformNode::construct(m_camera));
    graph.addNode(ForwardRenderNode::name(), ForwardRenderNode::construct(m_scene, staticResources));
    graph.addNode(FinalPostFxNode::name(), FinalPostFxNode::construct());
}

void TestApp::update(float elapsedTime, float deltaTime)
{
    ImGui::ShowDemoWindow();

    const Input& input = Input::instance();
    m_camera.update(input, GlobalState::get().windowExtent(), deltaTime);

    //mat4 matrix = mathkit::axisAngleMatrix({ 0, 1, 0 }, elapsedTime * 3.1415f / 2.0f) * mathkit::scale(50, 50, 50);
    mat4 matrix = mathkit::axisAngleMatrix({ 0, 1, 0 }, 3.0f / 4.0f * mathkit::PI) * mathkit::scale(50, 50, 50);
    m_model->transform().setLocalMatrix(matrix);

}
