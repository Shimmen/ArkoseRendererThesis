#include "App.h"
#include "AppResourceManager.h"
#include "Commands.h"
#include "RenderState.h"
#include "ResourceManager.h"

App::App(AppResourceManager& appResourceManager)
    : m_appResourceManager(appResourceManager)
{
}

void App::setup(const ApplicationState&)
{
    // Here we can do stuff like CPU work and GPU stuff that is fully or mostly static,
    // e.g. load textures, load meshes, set vertex buffers.

    //m_testTexture = m_resourceManager.loadTexture2D("test-pattern.png", false);

    m_vertexLayout = VertexLayout {
        sizeof(Vertex),
        { { 0, VertexAttributeType::Float4, offsetof(Vertex, position) },
            { 1, VertexAttributeType::Float3, offsetof(Vertex, color) },
            { 2, VertexAttributeType::Float2, offsetof(Vertex, texCoord) } }
    };

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

    m_indexCount = indices.size();
    m_vertexBuffer = m_appResourceManager.createStaticBuffer(Buffer::Usage::Vertex, std::move(vertices));
    m_indexBuffer = m_appResourceManager.createStaticBuffer(Buffer::Usage::Index, std::move(indices));

    m_shader = Shader::createBasic("basic", "example.vert", "example.frag");
}

void App::timeStepForFrame(const ApplicationState&)
{
    // Here we can do stuff like CPU work, and GPU work that is not pass specific,
    // e.g. update uniform buffers.
}

GpuPipeline App::createPipeline(const ApplicationState&)
{
    RenderState defaultState {};

    GpuPipeline pipeline {};

    pipeline.addRenderPass("TrianglePass", [&](ResourceManager& resourceManager) {
        RenderTarget windowTarget = resourceManager.getWindowRenderTarget();
        return [&](const ApplicationState& appState, RenderPass::CommandList& commandList) {
            commandList.push_back(std::make_unique<CmdDrawIndexed>(
                m_vertexBuffer,
                m_vertexLayout, // this?
                m_indexBuffer,
                m_indexCount,
                DrawMode::Triangles,
                defaultState, // this!
                m_shader)); // this?
        };
    });

    return pipeline;
}
