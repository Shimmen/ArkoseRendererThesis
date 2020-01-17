#include "TestApp.h"

#include "camera_state.h"
#include <utility/Input.h>

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

std::unique_ptr<RenderGraph> TestApp::mainRenderGraph()
{
    auto graph = std::make_unique<RenderGraph>();

    graph->addNode("example-triangle", [&](ResourceManager& resourceManager, const ApplicationState& appState) {
        // TODO: Well, now it seems very reasonable to actually include this in the resource manager..
        Shader shader = Shader::createBasic("basic", "example.vert", "example.frag");

        Buffer& cameraUniformBuffer = resourceManager.createBuffer(sizeof(CameraState), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
        Texture2D& testTexture = resourceManager.loadTexture2D("assets/test-pattern.png", true, false);

        VertexLayout vertexLayout = VertexLayout {
            sizeof(Vertex),
            { { 0, VertexAttributeType::Float4, offsetof(Vertex, position) },
                { 1, VertexAttributeType::Float3, offsetof(Vertex, color) },
                { 2, VertexAttributeType::Float2, offsetof(Vertex, texCoord) } }
        };

        ShaderBinding uniformBufferBinding = { 0, ShaderStage::Vertex, &cameraUniformBuffer };
        ShaderBinding textureSamplerBinding = { 1, ShaderStage::Fragment, &testTexture };
        ShaderBindingSet shaderBindingSet { uniformBufferBinding, textureSamplerBinding };

        // TODO: Create some builder class for these type of numerous (and often defaulted anyway) RenderState members

        Viewport viewport;
        viewport.width = appState.windowExtent().width();
        viewport.height = appState.windowExtent().height();

        BlendState blendState;
        blendState.enabled = false;

        RasterState rasterState;
        rasterState.polygonMode = PolygonMode::Filled;
        rasterState.backfaceCullingEnabled = false;
        //rasterState.frontFace = TriangleWindingOrder::CounterClockwise;

        //Texture2D& colorTexture = resourceManager.createTexture2D(appState.windowExtent(), Texture2D::Format::RGBA8);
        //Texture2D& depthTexture = resourceManager.createTexture2D(appState.windowExtent(), Texture2D::Format::Depth32F);
        //RenderTarget& renderTarget = resourceManager.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &colorTexture },
        //    { RenderTarget::AttachmentType::Depth, &depthTexture } });

        const RenderTarget& windowTarget = resourceManager.getWindowRenderTarget();
        RenderState& renderState = resourceManager.createRenderState(windowTarget, vertexLayout, shader, shaderBindingSet, viewport, blendState, rasterState);

        return [&](const ApplicationState& appState, CommandList& commandList, FrameAllocator& frameAllocator) {
            auto& cameraState = frameAllocator.allocate<CameraState>();
            cameraState.world_from_local = mathkit::axisAngle({ 0, 1, 0 }, appState.elapsedTime() * 3.1415f / 2.0f);
            if (Input::instance().isKeyDown(GLFW_KEY_UP)) {
                cameraState.world_from_local = mathkit::translate(0, 1, 0) * cameraState.world_from_local;
            }
            if (Input::instance().isKeyDown(GLFW_KEY_LEFT)) {
                cameraState.world_from_local = mathkit::translate(-0.5f, 0, 0) * cameraState.world_from_local;
            }
            if (Input::instance().isKeyDown(GLFW_KEY_RIGHT)) {
                cameraState.world_from_local = mathkit::translate(+0.5f, 0, 0) * cameraState.world_from_local;
            }
            cameraState.view_from_world = mathkit::lookAt({ 0, 1, 3 }, { 0, 0.5f, 0 });
            float aspectRatio = float(appState.windowExtent().width()) / float(appState.windowExtent().height());
            cameraState.projection_from_view = mathkit::infinitePerspective(mathkit::radians(45), aspectRatio, 0.1f);
            cameraState.view_from_local = cameraState.view_from_world * cameraState.world_from_local;
            cameraState.projection_from_local = cameraState.projection_from_view * cameraState.view_from_local;

            commandList.add<CmdUpdateBuffer>(cameraUniformBuffer, &cameraState, sizeof(CameraState));
            commandList.add<CmdSetRenderState>(renderState);
            commandList.add<CmdClear>(ClearColor(1.0f, 0.0f, 1.0f), 1.0f);
            commandList.add<CmdDrawIndexed>(
                *m_vertexBuffer,
                *m_indexBuffer,
                m_indexCount,
                DrawMode::Triangles);
        };
    });

    return graph;
}
