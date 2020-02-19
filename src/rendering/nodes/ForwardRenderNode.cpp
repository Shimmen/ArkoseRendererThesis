#include "ForwardRenderNode.h"

#include "CameraUniformNode.h"

std::string ForwardRenderNode::name()
{
    return "forward";
}

ForwardRenderNode::ForwardRenderNode(const Scene& scene)
    : RenderGraphNode(ForwardRenderNode::name())
    , m_scene(scene)
{
}

void ForwardRenderNode::constructNode(Registry& nodeReg)
{
    m_drawables.clear();
    m_materials.clear();
    m_textures.clear();

    for (int i = 0; i < m_scene.modelCount(); ++i) {
        const Model& model = *m_scene[i];
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

            drawable.vertexBuffer = &nodeReg.createBuffer(std::move(vertices), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);
            drawable.indexBuffer = &nodeReg.createBuffer(mesh.indexData(), Buffer::Usage::Index, Buffer::MemoryHint::GpuOptimal);
            drawable.indexCount = mesh.indexCount();

            // Create textures
            // TODO: Remove redundant textures!
            int baseColorIndex = m_textures.size();
            std::string baseColorPath = mesh.material().baseColor;
            Texture& baseColorTexture = nodeReg.loadTexture2D(baseColorPath, true, true);
            m_textures.push_back(&baseColorTexture);

            int normalMapIndex = m_textures.size();
            std::string normalMapPath = mesh.material().normalMap;
            Texture& normalMapTexture = nodeReg.loadTexture2D(normalMapPath, false, true);
            m_textures.push_back(&normalMapTexture);

            // Create material
            // TODO: Remove redundant materials!
            ForwardMaterial material {};
            material.baseColor = baseColorIndex;
            material.normalMap = normalMapIndex;
            drawable.materialIndex = m_materials.size();
            m_materials.push_back(material);

            m_drawables.push_back(drawable);
        });
    }

    if (m_drawables.size() > FORWARD_MAX_DRAWABLES) {
        LogErrorAndExit("ForwardRenderNode: we need to up the number of max drawables that can be handled in the forward pass! "
                        "We have %u, the capacity is %u.\n",
                        m_drawables.size(), FORWARD_MAX_DRAWABLES);
    }

    if (m_textures.size() > FORWARD_MAX_TEXTURES) {
        LogErrorAndExit("ForwardRenderNode: we need to up the number of max textures that can be handled in the forward pass! "
                        "We have %u, the capacity is %u.\n",
                        m_textures.size(), FORWARD_MAX_TEXTURES);
    }
}

RenderGraphNode::ExecuteCallback ForwardRenderNode::constructFrame(Registry& reg) const
{
    // TODO: Well, now it seems very reasonable to actually include this in the resource manager..
    Shader shader = Shader::createBasic("forward.vert", "forward.frag");

    VertexLayout vertexLayout = VertexLayout {
        sizeof(Vertex),
        { { 0, VertexAttributeType::Float3, offsetof(Vertex, position) },
          { 1, VertexAttributeType::Float2, offsetof(Vertex, texCoord) },
          { 2, VertexAttributeType ::Float3, offsetof(Vertex, normal) },
          { 3, VertexAttributeType ::Float4, offsetof(Vertex, tangent) } }
    };

    size_t perObjectBufferSize = m_drawables.size() * sizeof(PerForwardObject);
    Buffer& perObjectBuffer = reg.createBuffer(perObjectBufferSize, Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);

    size_t materialBufferSize = m_materials.size() * sizeof(ForwardMaterial);
    Buffer& materialBuffer = reg.createBuffer(materialBufferSize, Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);

    ShaderBinding cameraUniformBufferBinding = { 0, ShaderStageVertex, reg.getBuffer(CameraUniformNode::name(), "buffer") };
    ShaderBinding perObjectBufferBinding = { 1, ShaderStageVertex, &perObjectBuffer };
    ShaderBinding materialBufferBinding = { 2, ShaderStageFragment, &materialBuffer };
    ShaderBinding textureSamplerBinding = { 3, ShaderStageFragment, m_textures, FORWARD_MAX_TEXTURES };
    BindingSet& bindingSet = reg.createBindingSet({ cameraUniformBufferBinding, perObjectBufferBinding, materialBufferBinding, textureSamplerBinding });

    // TODO: Create some builder class for these type of numerous (and often defaulted anyway) RenderState members

    const RenderTarget& windowTarget = reg.windowRenderTarget();

    Viewport viewport;
    viewport.extent = windowTarget.extent();

    BlendState blendState;
    blendState.enabled = false;

    RasterState rasterState;
    rasterState.polygonMode = PolygonMode::Filled;
    rasterState.frontFace = TriangleWindingOrder::CounterClockwise;
    rasterState.backfaceCullingEnabled = true;

    Texture& colorTexture = reg.createTexture2D(windowTarget.extent(), Texture::Format::RGBA8, Texture::Usage::AttachAndSample);
    reg.publish("color", colorTexture);

    Texture& normalTexture = reg.createTexture2D(windowTarget.extent(), Texture::Format::RGBA8, Texture::Usage::AttachAndSample);
    reg.publish("normal", normalTexture);

    Texture& depthTexture = reg.createTexture2D(windowTarget.extent(), Texture::Format::Depth32F, Texture::Usage::AttachAndSample);
    RenderTarget& renderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &colorTexture },
                                                          { RenderTarget::AttachmentType::Color1, &normalTexture },
                                                          { RenderTarget::AttachmentType::Depth, &depthTexture } });

    RenderState& renderState = reg.createRenderState(renderTarget, vertexLayout, shader, { &bindingSet }, viewport, blendState, rasterState);

    return [&](const AppState& appState, CommandList& cmdList) {
        cmdList.setRenderState(renderState, ClearColor(0.1f, 0.1f, 0.1f), 1.0f);
        cmdList.bindSet(bindingSet, 0);

        cmdList.updateBufferImmediately(perObjectBuffer, (void*)m_materials.data(), m_materials.size() * sizeof(ForwardMaterial));

        size_t numDrawables = m_drawables.size();
        std::vector<PerForwardObject> perObjectData { numDrawables };
        for (int i = 0; i < numDrawables; ++i) {
            auto& drawable = m_drawables[i];
            perObjectData[i] = {
                .worldFromLocal = drawable.mesh->transform().worldMatrix(),
                .worldFromTangent = mat4(drawable.mesh->transform().normalMatrix()),
                .materialIndex = drawable.materialIndex
            };
        }
        cmdList.updateBufferImmediately(perObjectBuffer, perObjectData.data(), numDrawables * sizeof(PerForwardObject));

        for (int i = 0; i < numDrawables; ++i) {
            const Drawable& drawable = m_drawables[i];
            cmdList.drawIndexed(*drawable.vertexBuffer, *drawable.indexBuffer, drawable.indexCount, i);
        }
    };
}
