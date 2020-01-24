#include "TestApp.h"

#include "rendering/nodes/FinalPostFxNode.h"
#include "rendering/nodes/ForwardRendererNode.h"
#include "utility/GlobalState.h"
#include "utility/Input.h"
#include <imgui.h>

void TestApp::setup(StaticResourceManager& staticResources)
{
    // Here we can do stuff like CPU work and GPU stuff that is fully or mostly static,
    // e.g. load textures, load meshes, set vertex buffers.

    m_object.diffuseTexture = &staticResources.loadTexture2D("assets/test-pattern.png", true, true);

    std::vector<Vertex> vertices = {
        { vec3(-0.5, -0.5, 0), vec3(1, 0, 0), vec2(1, 0) },
        { vec3(0.5, -0.5, 0), vec3(0, 1, 0), vec2(0, 0) },
        { vec3(0.5, 0.5, 0), vec3(0, 0, 1), vec2(0, 1) },
        { vec3(-0.5, 0.5, 0), vec3(1, 1, 1), vec2(1, 1) },

        { { -0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f } },
        { { 0.5f, -0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } },
        { { 0.5f, 0.5f, -0.5f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
        { { -0.5f, 0.5f, -0.5f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } }
    };
    std::vector<uint16_t> indices = {
        0, 1, 2,
        2, 3, 0,

        4, 5, 6,
        6, 7, 4
    };

    m_object.indexCount = indices.size();
    m_object.vertexBuffer = &staticResources.createBuffer(Buffer::Usage::Vertex, std::move(vertices));
    m_object.indexBuffer = &staticResources.createBuffer(Buffer::Usage::Index, std::move(indices));

    m_scene.objects.push_back(m_object);
    m_scene.camera = &m_camera;

    m_camera.lookAt({ 0, 1, 3 }, { 0, 0.5f, 0 });
}

void TestApp::makeRenderGraph(RenderGraph& graph)
{
    graph.addNode("forward", ForwardRendererNode::construct(m_scene));
    graph.addNode("final", FinalPostFxNode::construct());
}

void TestApp::update(float elapsedTime, float deltaTime)
{
    ImGui::ShowDemoWindow();

    const Input& input = Input::instance();
    m_camera.update(input, GlobalState::get().windowExtent(), deltaTime);
}
