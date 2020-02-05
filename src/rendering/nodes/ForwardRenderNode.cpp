#include "ForwardRenderNode.h"

#include "CameraUniformNode.h"

std::string ForwardRenderNode::name()
{
    return "forward";
}

RenderGraphNode::NodeConstructorFunction ForwardRenderNode::construct(const Scene& scene)
{
    // TODO: Select implementation conditionally depending on what's supported!
    //return constructSlowImplementation(scene);
    return constructFastImplementation(scene);
}

RenderGraphNode::NodeConstructorFunction ForwardRenderNode::constructFastImplementation(const Scene& scene)
{
    return [&](Registry& registry) {
        //auto& state = registry.node.allocator().allocateSingle<State>();
        static State state {};
        //setupState(scene, registry.node, state);
        setupState(scene, registry.frame, state);

        // TODO: Well, now it seems very reasonable to actually include this in the resource manager..
        Shader shader = Shader::createBasic("forward", "forward.vert", "forward.frag");

        VertexLayout vertexLayout = VertexLayout {
            sizeof(Vertex),
            { { 0, VertexAttributeType::Float3, offsetof(Vertex, position) },
                { 1, VertexAttributeType::Float2, offsetof(Vertex, texCoord) },
                { 2, VertexAttributeType ::Float3, offsetof(Vertex, normal) },
                { 3, VertexAttributeType ::Float4, offsetof(Vertex, tangent) } }
        };

        size_t perObjectBufferSize = state.drawables.size() * sizeof(PerForwardObject);
        Buffer& perObjectBuffer = registry.frame.createBuffer(perObjectBufferSize, Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);

        size_t materialBufferSize = state.materials.size() * sizeof(ForwardMaterial);
        Buffer& materialBuffer = registry.frame.createBuffer(materialBufferSize, Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);

        ShaderBinding cameraUniformBufferBinding = { 0, ShaderStage::Vertex, registry.frame.getBuffer(CameraUniformNode::name(), "buffer") };
        ShaderBinding perObjectBufferBinding = { 1, ShaderStage::Vertex, &perObjectBuffer };
        ShaderBinding materialBufferBinding = { 2, ShaderStage::Fragment, &materialBuffer };
        ShaderBinding textureSamplerBinding = { 3, ShaderStage::Fragment, state.textures, FORWARD_MAX_TEXTURES };
        BindingSet& bindingSet = registry.frame.createBindingSet({ cameraUniformBufferBinding, perObjectBufferBinding, materialBufferBinding, textureSamplerBinding });

        // TODO: Create some builder class for these type of numerous (and often defaulted anyway) RenderState members

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

        RenderState& renderState = registry.frame.createRenderState(renderTarget, vertexLayout, shader, bindingSet, viewport, blendState, rasterState);

        return [&](const AppState& appState, CommandList& commandList) {
            commandList.add<CmdSetRenderState>(renderState);
            commandList.add<CmdClear>(ClearColor(0.1f, 0.1f, 0.1f), 1.0f);

            commandList.add<CmdBindSet>(0, bindingSet);

            commandList.add<CmdUpdateBuffer>(perObjectBuffer, state.materials.data(), state.materials.size() * sizeof(ForwardMaterial));

            int numDrawables = state.drawables.size();
            //PerForwardObject* perObjectData = registry.frame.allocator().allocate<PerForwardObject>(numDrawables);
            static std::vector<PerForwardObject> perObjectData {};
            perObjectData.resize(numDrawables);

            for (int i = 0; i < numDrawables; ++i) {
                auto& drawable = state.drawables[i];
                perObjectData[i] = {
                    .worldFromLocal = drawable.mesh->transform().worldMatrix(),
                    .worldFromTangent = mat4(drawable.mesh->transform().normalMatrix()),
                    .materialIndex = drawable.materialIndex
                };
            }
            commandList.add<CmdUpdateBuffer>(perObjectBuffer, perObjectData.data(), numDrawables * sizeof(PerForwardObject));

            for (int i = 0; i < numDrawables; ++i) {
                const Drawable& drawable = state.drawables[i];
                commandList.add<CmdDrawIndexed>(*drawable.vertexBuffer, *drawable.indexBuffer, drawable.indexCount, DrawMode::Triangles, i);
            }
        };
    };
}

void ForwardRenderNode::setupState(const Scene& scene, ResourceManager& staticResources, State& state)
{
    state.drawables.clear();
    state.materials.clear();
    state.textures.clear();

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

            drawable.vertexBuffer = &staticResources.createBuffer(std::move(vertices), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);
            drawable.indexBuffer = &staticResources.createBuffer(mesh.indexData(), Buffer::Usage::Index, Buffer::MemoryHint::GpuOptimal);
            drawable.indexCount = mesh.indexCount();

            // Create textures
            int baseColorIndex = state.textures.size();
            std::string baseColorPath = mesh.material().baseColor;
            Texture& baseColorTexture = staticResources.loadTexture2D(baseColorPath, true, true);
            state.textures.push_back(&baseColorTexture);

            int normalMapIndex = state.textures.size();
            std::string normalMapPath = mesh.material().normalMap;
            Texture& normalMapTexture = staticResources.loadTexture2D(normalMapPath, false, true);
            state.textures.push_back(&normalMapTexture);

            // Create material
            // TODO: Remove redundant materials!
            ForwardMaterial material {};
            material.baseColor = baseColorIndex;
            material.normalMap = normalMapIndex;
            drawable.materialIndex = state.materials.size();
            state.materials.push_back(material);

            state.drawables.push_back(drawable);
        });
    }

    if (state.drawables.size() > FORWARD_MAX_DRAWABLES) {
        LogErrorAndExit("ForwardRenderNode: we need to up the number of max drawables that can be handled in the forward pass! "
                        "We have %u, the capacity is %u.\n",
            state.drawables.size(), FORWARD_MAX_DRAWABLES);
    }

    if (state.textures.size() > FORWARD_MAX_TEXTURES) {
        LogErrorAndExit("ForwardRenderNode: we need to up the number of max textures that can be handled in the forward pass! "
                        "We have %u, the capacity is %u.\n",
            state.textures.size(), FORWARD_MAX_TEXTURES);
    }
}
