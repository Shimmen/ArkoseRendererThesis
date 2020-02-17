#include "SlowForwardRenderNode.h"
#include <imgui.h>

#include "CameraUniformNode.h"
#include "LightData.h"
#include "ShadowMapNode.h"

std::string SlowForwardRenderNode::name()
{
    return "forward";
}

RenderGraphBasicNode::ConstructorFunction SlowForwardRenderNode::construct(const Scene& scene)
{
    return [&](Registry& reg) {
        static State state {}; // TODO: Don't use static data like this!
        setupState(scene, reg, state); // TODO: Don't use the frame registry!

        Shader shader = Shader::createBasic("forwardSlow", "forwardSlow.vert", "forwardSlow.frag");

        VertexLayout vertexLayout = VertexLayout {
            sizeof(Vertex),
            { { 0, VertexAttributeType::Float3, offsetof(Vertex, position) },
                { 1, VertexAttributeType::Float2, offsetof(Vertex, texCoord) },
                { 2, VertexAttributeType ::Float3, offsetof(Vertex, normal) },
                { 3, VertexAttributeType ::Float4, offsetof(Vertex, tangent) } }
        };

        const RenderTarget& windowTarget = reg.windowRenderTarget();

        Viewport viewport;
        viewport.extent = windowTarget.extent();

        BlendState blendState;
        blendState.enabled = false;

        RasterState rasterState;
        rasterState.polygonMode = PolygonMode::Filled;
        rasterState.frontFace = TriangleWindingOrder::CounterClockwise;
        rasterState.backfaceCullingEnabled = true;

        Texture& colorTexture = reg.createTexture2D(windowTarget.extent(), Texture::Format::RGBA16F, Texture::Usage::All);
        reg.publish("color", colorTexture);

        Texture& normalTexture = reg.createTexture2D(windowTarget.extent(), Texture::Format::RGBA8, Texture::Usage::All);
        reg.publish("normal", normalTexture);

        Texture& depthTexture = reg.createTexture2D(windowTarget.extent(), Texture::Format::Depth32F, Texture::Usage::All);
        RenderTarget& renderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &colorTexture },
            { RenderTarget::AttachmentType::Color1, &normalTexture },
            { RenderTarget::AttachmentType::Depth, &depthTexture } });

        const Buffer* cameraUniformBuffer = reg.getBuffer(CameraUniformNode::name(), "buffer");
        BindingSet& fixedBindingSet = reg.createBindingSet({ { 0, ShaderStage(ShaderStageVertex | ShaderStageFragment), cameraUniformBuffer } });

        const Texture* dirLightShadowMap = reg.getTexture(ShadowMapNode::name(), "directional");
        Buffer& dirLightUniformBuffer = reg.createBuffer(sizeof(DirectionalLight), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
        BindingSet& dirLightBindingSet = reg.createBindingSet({ { 0, ShaderStageFragment, dirLightShadowMap }, { 1, ShaderStageFragment, &dirLightUniformBuffer } });

        std::vector<const BindingSet*> allBindingSets {};
        allBindingSets.push_back(&fixedBindingSet);
        //for (auto& drawable : state.drawables) { // TODO: We have to provide the layouts in order!!!
        allBindingSets.push_back(state.drawables[0].bindingSet);
        //}
        allBindingSets.push_back(&dirLightBindingSet);

        RenderState& renderState = reg.createRenderState(renderTarget, vertexLayout, shader, allBindingSets, viewport, blendState, rasterState);

        return [&](const AppState& appState, CommandList& cmdList) {
            cmdList.setRenderState(renderState, ClearColor(0.2f, 0.2f, 0.2f), 1.0f);
            cmdList.bindSet(fixedBindingSet, 0);

            DirectionalLight dirLight {
                .colorAndIntensity = { scene.sun().color, scene.sun().intensity },
                .viewSpaceDirection = scene.camera().viewMatrix() * normalize(vec4(scene.sun().direction, 0.0)),
                .lightProjectionFromWorld = scene.sun().lightProjection()
            };
            cmdList.updateBufferImmediately(dirLightUniformBuffer, &dirLight, sizeof(DirectionalLight));
            cmdList.bindSet(dirLightBindingSet, 2);

            for (const Drawable& drawable : state.drawables) {

                // TODO: Hmm, it still looks very much like it happens in line with the other commands..
                PerForwardObject objectData {
                    .worldFromLocal = drawable.mesh->transform().worldMatrix(),
                    .worldFromTangent = mat4(drawable.mesh->transform().normalMatrix())
                };
                cmdList.updateBufferImmediately(*drawable.objectDataBuffer, &objectData, sizeof(PerForwardObject));

                cmdList.bindSet(*drawable.bindingSet, 1);
                cmdList.drawIndexed(*drawable.vertexBuffer, *drawable.indexBuffer, drawable.indexCount);
            }
        };
    };
}

void SlowForwardRenderNode::setupState(const Scene& scene, Registry& resources, State& state)
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
            std::string metallicRoughnessPath = mesh.material().metallicRoughness;
            Texture& metallicRoughnessTexture = resources.loadTexture2D(metallicRoughnessPath, false, true);
            std::string emissivePath = mesh.material().emissive;
            Texture& emissiveTexture = resources.loadTexture2D(emissivePath, true, true);

            // Create binding set
            drawable.bindingSet = &resources.createBindingSet(
                { { 0, ShaderStageVertex, drawable.objectDataBuffer },
                    { 1, ShaderStageFragment, &baseColorTexture },
                    { 2, ShaderStageFragment, &normalMapTexture },
                    { 3, ShaderStageFragment, &metallicRoughnessTexture },
                    { 4, ShaderStageFragment, &emissiveTexture } });

            state.drawables.push_back(drawable);
        });
    }
}
