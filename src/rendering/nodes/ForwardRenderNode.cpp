#include "ForwardRenderNode.h"

#include "CameraUniformNode.h"

std::string ForwardRenderNode::name()
{
    return "forward";
}

RenderGraphNode::NodeConstructorFunction ForwardRenderNode::construct(const Scene& scene, StaticResourceManager& staticResources)
{
    ASSERT(scene.modelCount() == 1);
    const Model& model = *scene[0];
    const Mesh& mesh = *model[0];

    std::vector<Vertex> vertices {};
    {
        auto posData = mesh.positionData();
        auto texData = mesh.texcoordData();
        ASSERT(posData.size() == texData.size());
        for (int i = 0; i < posData.size(); ++i) {
            Vertex vertex {};
            vertex.position = posData[i];
            vertex.texCoord = texData[i];
            vertices.push_back(vertex);
        }
    }

    Buffer& vertexBuffer = staticResources.createBuffer(Buffer::Usage::Vertex, std::move(vertices));
    Buffer& indexBuffer = staticResources.createBuffer(Buffer::Usage::Index, mesh.indexData());

    Texture& diffuseTexture = staticResources.loadTexture2D("assets/BoomBox/BoomBoxWithAxes_baseColor.png", true, true);

    return [&](ResourceManager& resourceManager) {
        // TODO: Well, now it seems very reasonable to actually include this in the resource manager..
        Shader shader = Shader::createBasic("basic", "example.vert", "example.frag");

        VertexLayout vertexLayout = VertexLayout {
            sizeof(Vertex),
            { { 0, VertexAttributeType::Float4, offsetof(Vertex, position) },
                { 1, VertexAttributeType::Float2, offsetof(Vertex, texCoord) } }
        };

        ShaderBinding uniformBufferBinding = { 0, ShaderStage::Vertex, resourceManager.getBuffer(CameraUniformNode::name(), "buffer") };
        ShaderBinding textureSamplerBinding = { 1, ShaderStage::Fragment, &diffuseTexture };
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
            commandList.add<CmdSetRenderState>(renderState);
            commandList.add<CmdClear>(ClearColor(0.1f, 0.1f, 0.1f), 1.0f);
            commandList.add<CmdDrawIndexed>(vertexBuffer, indexBuffer, mesh.indexCount(), DrawMode::Triangles);
        };
    };
}
