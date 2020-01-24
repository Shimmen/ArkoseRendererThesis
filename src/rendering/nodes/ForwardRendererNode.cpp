#include "ForwardRendererNode.h"

#include "camera_state.h"

RenderGraphNode::NodeConstructorFunction ForwardRendererNode::construct(const ForwardRendererNode::Scene& scene)
{
    ASSERT(scene.objects.size() == 1);
    const Object& object = scene.objects[0];

    return [&](ResourceManager& resourceManager) {
        // TODO: Well, now it seems very reasonable to actually include this in the resource manager..
        Shader shader = Shader::createBasic("basic", "example.vert", "example.frag");

        // TODO: Move this buffer (and the updating of it) to its own node!
        Buffer& cameraUniformBuffer = resourceManager.createBuffer(sizeof(CameraState), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);

        VertexLayout vertexLayout = VertexLayout {
            sizeof(Vertex),
            { { 0, VertexAttributeType::Float4, offsetof(Vertex, position) },
                { 1, VertexAttributeType::Float3, offsetof(Vertex, color) },
                { 2, VertexAttributeType::Float2, offsetof(Vertex, texCoord) } }
        };

        ShaderBinding uniformBufferBinding = { 0, ShaderStage::Vertex, &cameraUniformBuffer };
        ShaderBinding textureSamplerBinding = { 1, ShaderStage::Fragment, object.diffuseTexture };
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

        Texture& colorTexture = resourceManager.createTexture2D(windowTarget.extent(), Texture::Format::RGBA8, Texture::Usage::All);
        resourceManager.publish("color", colorTexture);

        Texture& depthTexture = resourceManager.createTexture2D(windowTarget.extent(), Texture::Format::Depth32F, Texture::Usage::All);
        RenderTarget& renderTarget = resourceManager.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &colorTexture },
            { RenderTarget::AttachmentType::Depth, &depthTexture } });

        RenderState& renderState = resourceManager.createRenderState(renderTarget, vertexLayout, shader, shaderBindingSet, viewport, blendState, rasterState);

        return [&](const ApplicationState& appState, CommandList& commandList, FrameAllocator& frameAllocator) {
            auto& cameraState = frameAllocator.allocate<CameraState>();

            cameraState.world_from_local = mathkit::axisAngleMatrix({ 0, 1, 0 }, appState.elapsedTime() * 3.1415f / 2.0f);
            cameraState.view_from_world = scene.camera->viewMatrix();
            cameraState.projection_from_view = scene.camera->projectionMatrix();
            cameraState.view_from_local = cameraState.view_from_world * cameraState.world_from_local;
            cameraState.projection_from_local = cameraState.projection_from_view * cameraState.view_from_local;
            commandList.add<CmdUpdateBuffer>(cameraUniformBuffer, &cameraState, sizeof(CameraState));

            commandList.add<CmdSetRenderState>(renderState);
            commandList.add<CmdClear>(ClearColor(0.1f, 0.1f, 0.1f), 1.0f);
            commandList.add<CmdDrawIndexed>(*object.vertexBuffer, *object.indexBuffer, object.indexCount, DrawMode::Triangles);
        };
    };
}
