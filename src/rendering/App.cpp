#include "App.h"
#include "Commands.h"
#include "RenderState.h"
#include "rendering/ResourceManager.h"

App::App()
{
}

void App::setup(ApplicationState appState)
{
    //m_testTexture = m_resourceManager.loadTexture2D("test-pattern.png", false);

    struct Vertex {
        vec3 position;
        vec3 color;
        vec2 texCoord;
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

    Buffer vertexBuffer = m_resourceManager->createBuffer(vertices, Buffer::Usage::GpuOptimal);
    Buffer indexBuffer = m_resourceManager->createBuffer(indices, Buffer::Usage::GpuOptimal);

    RenderState defaultState {};
    Shader shader = Shader::createBasic("basic", "example.vert", "example.frag");

    m_renderPass = std::make_unique<RenderPass>([&](ResourceManager& resourceManager) {
        RenderTarget windowTarget = resourceManager.getWindowRenderTarget();

        return [&](const ApplicationState& appState, RenderPass::CommandList& commandList) {
            VertexLayout vertexLayout = {
                sizeof(Vertex),
                { { 0, VertexAttributeType::Float4, offsetof(Vertex, position) },
                    { 1, VertexAttributeType::Float3, offsetof(Vertex, color) },
                    { 2, VertexAttributeType::Float2, offsetof(Vertex, texCoord) } }
            };

            commandList.push_back(std::make_unique<CmdDrawIndexed>(
                vertexBuffer,
                vertexLayout,
                indexBuffer,
                indices.size(),
                DrawMode::Triangles,
                defaultState,
                shader));
        };
    });
}

void App::drawFrame(ApplicationState appState)
{
}
