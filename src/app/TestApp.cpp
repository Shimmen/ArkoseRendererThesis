#include "TestApp.h"

#include "camera_state.h"

void TestApp::setup(StaticResourceManager& staticResources)
{
    // Here we can do stuff like CPU work and GPU stuff that is fully or mostly static,
    // e.g. load textures, load meshes, set vertex buffers.

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
    m_vertexBuffer = &staticResources.createBuffer(Buffer::Usage::Vertex, std::move(vertices));
    m_indexBuffer = &staticResources.createBuffer(Buffer::Usage::Index, std::move(indices));
}

RenderGraph TestApp::createPipeline(const ApplicationState& appState)
{
    RenderGraph graph {};

    graph.addNode("example-triangle", [&](ResourceManager& resourceManager) {

        // TODO: Well, now it seems very reasonable to actually include this in the resource manager..
        Shader shader = Shader::createBasic("basic", "example.vert", "example.frag");

        Buffer& cameraUniformBuffer = resourceManager.createBuffer(sizeof(CameraState), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
        Texture2D& testTexture = resourceManager.loadTexture2D("test-pattern.png", true, false);

        VertexLayout vertexLayout = VertexLayout {
            sizeof(Vertex),
            { { 0, VertexAttributeType::Float4, offsetof(Vertex, position) },
                { 1, VertexAttributeType::Float3, offsetof(Vertex, color) },
                { 2, VertexAttributeType::Float2, offsetof(Vertex, texCoord) } }
        };

        ShaderBinding uniformBufferBinding = { 0, ShaderFileType::Vertex, &cameraUniformBuffer };
        ShaderBinding textureSamplerBinding = { 1, ShaderFileType::Fragment, &testTexture };
        ShaderBindingSet shaderBindingSet { uniformBufferBinding, textureSamplerBinding };

        Viewport viewport;
        viewport.width = appState.windowExtent.width();
        viewport.height = appState.windowExtent.height();

        BlendState blendState;
        blendState.enabled = false;

        RenderTarget& windowTarget = resourceManager.getWindowRenderTarget();
        RenderState& renderState = resourceManager.createRenderState(windowTarget, vertexLayout, shader, shaderBindingSet, viewport, blendState);

        return [&](const ApplicationState& appState, CommandList& commandList) {

            // TODO: This doesn't seem optimal! We can't have it on the stack because stack allocated data will go out of scope,
            //  and we don't wanna heap alloc every frame. Hmm, let's think about that. Maybe have some custom arena allocator for that?
            static CameraState cameraState {};

            cameraState.world_from_local = mathkit::axisAngle({ 0, 1, 0 }, appState.timeSinceStartup * 3.1415f / 2.0f);
            cameraState.view_from_world = mathkit::lookAt({ 0, 1, 2 }, { 0, 0, 0 });
            float aspectRatio = float(appState.windowExtent.width()) / float(appState.windowExtent.height());
            cameraState.projection_from_view = mathkit::infinitePerspective(mathkit::radians(45), aspectRatio, 0.1f);
            cameraState.view_from_local = cameraState.view_from_world * cameraState.world_from_local;
            cameraState.projection_from_local = cameraState.projection_from_view * cameraState.view_from_local;

            commandList.add<CmdUpdateBuffer>(cameraUniformBuffer, &cameraState, sizeof(CameraState));
            commandList.add<CmdSetRenderState>(renderState);
            commandList.add<CmdDrawIndexed>(
                *m_vertexBuffer,
                *m_indexBuffer,
                m_indexCount,
                DrawMode::Triangles);
        };
    });

    return graph;
}
