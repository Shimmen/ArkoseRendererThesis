#include "SlowForwardRenderNode.h"

#include "ForwardRenderNode.h"
#include "LightData.h"
#include "SceneUniformNode.h"
#include "ShadowMapNode.h"
#include <imgui.h>

SlowForwardRenderNode::SlowForwardRenderNode(const Scene& scene)
    : RenderGraphNode(ForwardRenderNode::name())
    , m_scene(scene)
{
}

void SlowForwardRenderNode::constructNode(Registry& nodeReg)
{
    m_drawables.clear();

    for (int i = 0; i < m_scene.modelCount(); ++i) {
        const Model& model = *m_scene[i];
        model.forEachMesh([&](const Mesh& mesh) {
            std::vector<Vertex> vertices {};
            {
                auto posData = mesh.positionData();
                auto texData = mesh.texcoordData();
                auto normalData = mesh.normalData();
                auto tangentData = mesh.tangentData();

                size_t texSize = texData.size();
                size_t normalSize = normalData.size();
                size_t tangentSize = tangentData.size();
                //ASSERT(posData.size() == texData.size());
                //ASSERT(posData.size() == normalData.size());
                //ASSERT(posData.size() == tangentData.size());

                for (int i = 0; i < posData.size(); ++i) {

                    vec2 tex = (i < texSize) ? texData[i] : vec2(0.0f);
                    vec3 norm = (i < normalSize) ? normalData[i] : vec3(0.0f);
                    vec4 tang = (i < tangentSize) ? tangentData[i] : vec4(0.0f);

                    vertices.push_back({ .position = posData[i],
                                         .texCoord = tex,
                                         .normal = norm,
                                         .tangent = tang });
                }
            }

            Drawable drawable {};
            drawable.mesh = &mesh;

            drawable.vertexBuffer = &nodeReg.createBuffer(std::move(vertices), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);
            drawable.indexBuffer = &nodeReg.createBuffer(mesh.indexData(), Buffer::Usage::Index, Buffer::MemoryHint::GpuOptimal);
            drawable.indexCount = mesh.indexCount();

            drawable.objectDataBuffer = &nodeReg.createBuffer(sizeof(PerForwardObject), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);

            const Material& material = mesh.material();

            // Create & load textures
            std::string baseColorPath = material.baseColor;
            Texture* baseColorTexture { nullptr };
            if (baseColorPath.empty()) {
                // the color is already in linear sRGB so we don't want to make an sRGB texture for it!
                baseColorTexture = &nodeReg.createPixelTexture(material.baseColorFactor, false);
            } else {
                baseColorTexture = &nodeReg.loadTexture2D(baseColorPath, true, true);
            }

            std::string normalMapPath = material.normalMap;
            Texture& normalMapTexture = nodeReg.loadTexture2D(normalMapPath, false, true);
            std::string metallicRoughnessPath = material.metallicRoughness;
            Texture& metallicRoughnessTexture = nodeReg.loadTexture2D(metallicRoughnessPath, false, true);
            std::string emissivePath = material.emissive;
            Texture& emissiveTexture = nodeReg.loadTexture2D(emissivePath, true, true);

            // Create binding set
            drawable.bindingSet = &nodeReg.createBindingSet(
                { { 0, ShaderStageVertex, drawable.objectDataBuffer },
                  { 1, ShaderStageFragment, baseColorTexture },
                  { 2, ShaderStageFragment, &normalMapTexture },
                  { 3, ShaderStageFragment, &metallicRoughnessTexture },
                  { 4, ShaderStageFragment, &emissiveTexture } });

            m_drawables.push_back(drawable);
        });
    }
}

RenderGraphNode::ExecuteCallback SlowForwardRenderNode::constructFrame(Registry& reg) const
{
    const RenderTarget& windowTarget = reg.windowRenderTarget();

    Texture& colorTexture = reg.createTexture2D(windowTarget.extent(), Texture::Format::RGBA16F, Texture::Usage::AttachAndSample);
    reg.publish("color", colorTexture);

    Texture& normalTexture = reg.createTexture2D(windowTarget.extent(), Texture::Format::RGBA16F, Texture::Usage::AttachAndSample);
    reg.publish("normal", normalTexture);

    Texture& depthTexture = reg.createTexture2D(windowTarget.extent(), Texture::Format::Depth32F, Texture::Usage::AttachAndSample);
    reg.publish("depth", depthTexture);

    Texture& baseColorTexture = reg.createTexture2D(windowTarget.extent(), Texture::Format::RGBA8, Texture::Usage::AttachAndSample);
    reg.publish("baseColor", baseColorTexture);

    RenderTarget& renderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &colorTexture },
                                                          { RenderTarget::AttachmentType::Color1, &normalTexture },
                                                          { RenderTarget::AttachmentType::Color2, &baseColorTexture },
                                                          { RenderTarget::AttachmentType::Depth, &depthTexture } });

    const Buffer* cameraUniformBuffer = reg.getBuffer(SceneUniformNode::name(), "camera");
    BindingSet& fixedBindingSet = reg.createBindingSet({ { 0, ShaderStage(ShaderStageVertex | ShaderStageFragment), cameraUniformBuffer } });

    const Texture* shadowMap = reg.getTexture(ShadowMapNode::name(), "directional").value_or(&reg.createPixelTexture(vec4(1.0), false));
    BindingSet& dirLightBindingSet = reg.createBindingSet({ { 0, ShaderStageFragment, shadowMap },
                                                            { 1, ShaderStageFragment, reg.getBuffer(SceneUniformNode::name(), "directionalLight") } });

    const Texture* spotShadowMap = reg.getTexture(ShadowMapNode::name(), "spot").value_or(&reg.createPixelTexture(vec4(1.0), false));
    BindingSet& spotLightBindingSet = reg.createBindingSet({ { 0, ShaderStageFragment, spotShadowMap },
                                                             { 1, ShaderStageFragment, reg.getBuffer(SceneUniformNode::name(), "spotLight") } });

    Shader shader = Shader::createBasic("forwardSlow.vert", "forwardSlow.frag");
    VertexLayout vertexLayout = VertexLayout {
        sizeof(Vertex),
        { { 0, VertexAttributeType::Float3, offsetof(Vertex, position) },
          { 1, VertexAttributeType::Float2, offsetof(Vertex, texCoord) },
          { 2, VertexAttributeType ::Float3, offsetof(Vertex, normal) },
          { 3, VertexAttributeType ::Float4, offsetof(Vertex, tangent) } }
    };

    RenderStateBuilder renderStateBuilder { renderTarget, shader, vertexLayout };
    renderStateBuilder.polygonMode = PolygonMode::Filled;

    // TODO: Clean up, we no longer have the same constraints as before
    renderStateBuilder
        .addBindingSet(fixedBindingSet)
        .addBindingSet(*m_drawables[0].bindingSet)
        .addBindingSet(dirLightBindingSet)
        .addBindingSet(spotLightBindingSet);

    RenderState& renderState = reg.createRenderState(renderStateBuilder);

    return [&](const AppState& appState, CommandList& cmdList) {
        static float ambientAmount = 0.0f;
        ImGui::SliderFloat("Ambient", &ambientAmount, 0.0f, 1.0f);
        static bool writeColor = true;
        ImGui::Checkbox("Write color", &writeColor);
        static bool forceDiffuse = false;
        ImGui::Checkbox("Force diffuse materials", &forceDiffuse);

        cmdList.setRenderState(renderState, ClearColor(0, 0, 0, 0), 1.0f);
        cmdList.bindSet(fixedBindingSet, 0);
        cmdList.bindSet(dirLightBindingSet, 2);
        cmdList.bindSet(spotLightBindingSet, 3);

        for (const Drawable& drawable : m_drawables) {

            // TODO: Hmm, it still looks very much like it happens in line with the other commands..
            PerForwardObject objectData {
                .worldFromLocal = drawable.mesh->transform().worldMatrix(),
                .worldFromTangent = mat4(drawable.mesh->transform().worldNormalMatrix())
            };
            cmdList.updateBufferImmediately(*drawable.objectDataBuffer, &objectData, sizeof(PerForwardObject));

            cmdList.pushConstant(ShaderStageFragment, writeColor, 0);
            cmdList.pushConstant(ShaderStageFragment, forceDiffuse, 4);
            cmdList.pushConstant(ShaderStageFragment, ambientAmount, 8);

            cmdList.bindSet(*drawable.bindingSet, 1);
            cmdList.drawIndexed(*drawable.vertexBuffer, *drawable.indexBuffer, drawable.indexCount, drawable.mesh->indexType());
        }
    };
}
