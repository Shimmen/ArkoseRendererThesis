#include "SlowForwardRenderNode.h"

#include "CameraUniformNode.h"

std::string SlowForwardRenderNode::name()
{
    return "forward";
}

RenderGraphNode::NodeConstructorFunction SlowForwardRenderNode::construct(const Scene& scene)
{
    return [&](Registry& registry) {
        static State state {}; // TODO: Don't use static data like this!
        setupState(scene, registry.frame, state); // TODO: Don't use the frame registry!

        Shader shader = Shader::createBasic("forwardSlow", "forwardSlow.vert", "forwardSlow.frag");

        VertexLayout vertexLayout = VertexLayout {
            sizeof(Vertex),
            { { 0, VertexAttributeType::Float3, offsetof(Vertex, position) },
                { 1, VertexAttributeType::Float2, offsetof(Vertex, texCoord) },
                { 2, VertexAttributeType ::Float3, offsetof(Vertex, normal) },
                { 3, VertexAttributeType ::Float4, offsetof(Vertex, tangent) } }
        };

        const RenderTarget& windowTarget = registry.frame.windowRenderTarget();

        Viewport viewport;
        viewport.extent = windowTarget.extent();

        BlendState blendState;
        blendState.enabled = false;

        RasterState rasterState;
        rasterState.polygonMode = PolygonMode::Filled;
        rasterState.frontFace = TriangleWindingOrder::CounterClockwise;
        rasterState.backfaceCullingEnabled = true;

        Texture& colorTexture = registry.frame.createTexture2D(windowTarget.extent(), Texture::Format::RGBA8, Texture::Usage::All);
        registry.frame.publish("color", colorTexture);

        Texture& normalTexture = registry.frame.createTexture2D(windowTarget.extent(), Texture::Format::RGBA8, Texture::Usage::All);
        registry.frame.publish("normal", normalTexture);

        Texture& depthTexture = registry.frame.createTexture2D(windowTarget.extent(), Texture::Format::Depth32F, Texture::Usage::All);
        RenderTarget& renderTarget = registry.frame.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &colorTexture },
            { RenderTarget::AttachmentType::Color1, &normalTexture },
            { RenderTarget::AttachmentType::Depth, &depthTexture } });

        // TODO: Actually have *ALL* things for the call!
        //  Or really, we just want the set of unique DESCRIPTOR LAYOUTS.
        BindingSet& allBindingSets = *state.drawables.front().bindingSet;

        RenderState& renderState = registry.frame.createRenderState(renderTarget, vertexLayout, shader, allBindingSets, viewport, blendState, rasterState);

        return [&](const AppState& appState, CommandList& cmdList) {
            cmdList.setRenderState(renderState, ClearColor(0.5f, 0.5f, 0.5f), 1.0f);

            //for (const Drawable& drawable : state.drawables) {
            for (int i = 0; i < 1/*state.drawables.size()*/; ++i) {
                const Drawable& drawable = state.drawables[i];

                // TODO: Hmm, it still looks very much like it happens in line with the other commands..
                PerForwardObject objectData {
                    .worldFromLocal = drawable.mesh->transform().worldMatrix(),
                    .worldFromTangent = mat4(drawable.mesh->transform().normalMatrix())
                };
                cmdList.updateBufferImmediately(*drawable.objectDataBuffer, &objectData, sizeof(PerForwardObject));

                cmdList.bindSet(*drawable.bindingSet, 0);
                cmdList.drawIndexed(*drawable.vertexBuffer, *drawable.indexBuffer, drawable.indexCount);
            }
        };
    };
}

void SlowForwardRenderNode::setupState(const Scene& scene, ResourceManager& resources, State& state)
{
    state.drawables.clear();

    for (int i = 0; i < scene.modelCount(); ++i) {
        const Model& model = *scene[i];
        model.forEachMesh([&](const Mesh& mesh) {
            std::vector<Vertex> vertices {};
            {
                auto posData = mesh.positionData();
                auto texData = mesh.texcoordData();
                auto normalData = mesh.normalData();
                auto tangentData = mesh.tangentData();

                ASSERT(posData.size() == texData.size());
                ASSERT(posData.size() == normalData.size());
                ASSERT(posData.size() == tangentData.size());

                for (int i = 0; i < posData.size(); ++i) {
                    vertices.push_back({ .position = posData[i],
                        .texCoord = texData[i],
                        .normal = normalData[i],
                        .tangent = tangentData[i] });
                }
            }

            Drawable drawable {};
            drawable.mesh = &mesh;

            drawable.vertexBuffer = &resources.createBuffer(std::move(vertices), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);
            drawable.indexBuffer = &resources.createBuffer(mesh.indexData(), Buffer::Usage::Index, Buffer::MemoryHint::GpuOptimal);
            drawable.indexCount = mesh.indexCount();

            drawable.objectDataBuffer = &resources.createBuffer(sizeof(PerForwardObject), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);

            // Create & load textures
            std::string baseColorPath = mesh.material().baseColor;
            Texture& baseColorTexture = resources.loadTexture2D(baseColorPath, true, true);
            std::string normalMapPath = mesh.material().normalMap;
            Texture& normalMapTexture = resources.loadTexture2D(normalMapPath, false, true);

            // Create binding set
            drawable.bindingSet = &resources.createBindingSet(
                { { 0, ShaderStage::Vertex, resources.getBuffer(CameraUniformNode::name(), "buffer") },
                    { 1, ShaderStage::Vertex, drawable.objectDataBuffer },
                    { 2, ShaderStage::Fragment, &baseColorTexture },
                    { 3, ShaderStage::Fragment, &normalMapTexture } });

            state.drawables.push_back(drawable);
        });
    }
}
