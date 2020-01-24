#include "TestApp.h"

#include "camera_state.h"
#include "utility/GlobalState.h"
#include "utility/Input.h"
#include <imgui.h>

void TestApp::setup(StaticResourceManager& staticResources)
{
    // Here we can do stuff like CPU work and GPU stuff that is fully or mostly static,
    // e.g. load textures, load meshes, set vertex buffers.

    m_testTexture = &staticResources.loadTexture2D("assets/test-pattern.png", true, true);

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

    m_camera.lookAt({ 0, 1, 3 }, { 0, 0.5f, 0 });
}

void TestApp::makeRenderGraph(RenderGraph& graph)
{
    graph.addNode("example-triangle", [&](ResourceManager& resourceManager) {
        // TODO: Well, now it seems very reasonable to actually include this in the resource manager..
        Shader shader = Shader::createBasic("basic", "example.vert", "example.frag");

        Buffer& cameraUniformBuffer = resourceManager.createBuffer(sizeof(CameraState), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);

        VertexLayout vertexLayout = VertexLayout {
            sizeof(Vertex),
            { { 0, VertexAttributeType::Float4, offsetof(Vertex, position) },
                { 1, VertexAttributeType::Float3, offsetof(Vertex, color) },
                { 2, VertexAttributeType::Float2, offsetof(Vertex, texCoord) } }
        };

        ShaderBinding uniformBufferBinding = { 0, ShaderStage::Vertex, &cameraUniformBuffer };
        ShaderBinding textureSamplerBinding = { 1, ShaderStage::Fragment, m_testTexture };
        ShaderBindingSet shaderBindingSet { uniformBufferBinding, textureSamplerBinding };

        // TODO: Create some builder class for these type of numerous (and often defaulted anyway) RenderState members

        const RenderTarget& windowTarget = resourceManager.windowRenderTarget();

        Viewport viewport;
        viewport.extent = windowTarget.extent();

        BlendState blendState;
        blendState.enabled = false;

        RasterState rasterState;
        rasterState.polygonMode = PolygonMode::Filled;
        rasterState.backfaceCullingEnabled = false;
        //rasterState.frontFace = TriangleWindingOrder::CounterClockwise;

        Texture& colorTexture = resourceManager.createTexture2D(windowTarget.extent(), Texture::Format::RGBA8, Texture::Usage::All);
        Texture& depthTexture = resourceManager.createTexture2D(windowTarget.extent(), Texture::Format::Depth32F, Texture::Usage::All);
        RenderTarget& renderTarget = resourceManager.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &colorTexture },
            { RenderTarget::AttachmentType::Depth, &depthTexture } });

        RenderState& renderState = resourceManager.createRenderState(windowTarget, vertexLayout, shader, shaderBindingSet, viewport, blendState, rasterState);
        //RenderState& renderState = resourceManager.createRenderState(renderTarget, vertexLayout, shader, shaderBindingSet, viewport, blendState, rasterState);

        return [&](const ApplicationState& appState, CommandList& commandList, FrameAllocator& frameAllocator) {
            auto& cameraState = frameAllocator.allocate<CameraState>();

            cameraState.world_from_local = mathkit::axisAngleMatrix({ 0, 1, 0 }, appState.elapsedTime() * 3.1415f / 2.0f);
            if (Input::instance().isKeyDown(GLFW_KEY_UP)) {
                cameraState.world_from_local = mathkit::translate(0, 0, -30) * cameraState.world_from_local;
            }
            if (Input::instance().isKeyDown(GLFW_KEY_LEFT)) {
                cameraState.world_from_local = mathkit::translate(-0.5f, 0, 0) * cameraState.world_from_local;
            }
            if (Input::instance().isKeyDown(GLFW_KEY_RIGHT)) {
                cameraState.world_from_local = mathkit::translate(+0.5f, 0, 0) * cameraState.world_from_local;
            }

            cameraState.view_from_world = m_camera.viewMatrix();
            cameraState.projection_from_view = m_camera.projectionMatrix();
            cameraState.view_from_local = cameraState.view_from_world * cameraState.world_from_local;
            cameraState.projection_from_local = cameraState.projection_from_view * cameraState.view_from_local;

            commandList.add<CmdUpdateBuffer>(cameraUniformBuffer, &cameraState, sizeof(CameraState));
            commandList.add<CmdSetRenderState>(renderState);
            commandList.add<CmdClear>(ClearColor(0.1f, 0.1f, 0.1f), 1.0f);
            commandList.add<CmdDrawIndexed>(
                *m_vertexBuffer,
                *m_indexBuffer,
                m_indexCount,
                DrawMode::Triangles);

            //const Texture& windowColorTexture = *windowTarget.attachment(RenderTarget::AttachmentType::Color0);
            //commandList.add<CmdCopyTexture>(colorTexture, windowColorTexture);
        };
    });
}

void TestApp::update(float elapsedTime, float deltaTime)
{
    ImGui::ShowDemoWindow();

    const Input& input = Input::instance();
    m_camera.update(input, GlobalState::get().windowExtent(), deltaTime);
}
