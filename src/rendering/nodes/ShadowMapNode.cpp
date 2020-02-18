#include "ShadowMapNode.h"

#include "ShadowData.h"
#include "utility/mathkit.h"

std::string ShadowMapNode::name()
{
    return "shadow";
}

ShadowMapNode::ShadowMapNode(const Scene& scene)
    : RenderGraphNode(ShadowMapNode::name())
    , m_scene(scene)
{
}

void ShadowMapNode::constructNode(Registry& nodeReg)
{
    m_drawables.clear();

    m_scene.forEachDrawable([&](int, const Mesh& mesh) {
        Drawable drawable {};
        drawable.mesh = &mesh;

        drawable.vertexBuffer = &nodeReg.createBuffer(mesh.positionData(), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);
        drawable.indexBuffer = &nodeReg.createBuffer(mesh.indexData(), Buffer::Usage::Index, Buffer::MemoryHint::GpuOptimal);
        drawable.indexCount = mesh.indexCount();

        m_drawables.push_back(drawable);
    });
}

RenderGraphNode::ExecuteCallback ShadowMapNode::constructFrame(Registry& reg) const
{
    const SunLight& sunLight = m_scene.sun();

    Texture& shadowMap = reg.createTexture2D(sunLight.shadowMapSize, Texture::Format::Depth32F, Texture::Usage::AttachAndSample);
    reg.publish("directional", shadowMap);

    Buffer& lightDataBuffer = reg.createBuffer(sizeof(mat4), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    BindingSet& lightBindingSet = reg.createBindingSet({ { 0, ShaderStageVertex, &lightDataBuffer } });

    Buffer& transformDataBuffer = reg.createBuffer(m_drawables.size() * sizeof(mat4), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    BindingSet& transformBindingSet = reg.createBindingSet({ { 0, ShaderStageVertex, &transformDataBuffer } });

    const RenderTarget& shadowRenderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Depth, &shadowMap } });
    Shader shader = Shader::createVertexOnly("shadowSun.vert");
    VertexLayout vertexLayout = VertexLayout { sizeof(vec3), { { 0, VertexAttributeType::Float3, 0 } } };

    RenderStateBuilder renderStateBuilder { shadowRenderTarget, shader, vertexLayout };
    renderStateBuilder
        .addBindingSet(lightBindingSet)
        .addBindingSet(transformBindingSet);

    RenderState& renderState = reg.createRenderState(renderStateBuilder);

    return [&](const AppState& appState, CommandList& cmdList) {
        cmdList.setRenderState(renderState, ClearColor(1, 0, 1), 1.0f);

        mat4 lightProjectionFromWorld = sunLight.lightProjection();
        cmdList.updateBufferImmediately(lightDataBuffer, &lightProjectionFromWorld, sizeof(mat4));
        cmdList.bindSet(lightBindingSet, 0);

        mat4 objectTransforms[SHADOW_MAX_OCCLUDERS];
        for (uint32_t idx = 0; idx < m_drawables.size(); ++idx) {
            objectTransforms[idx] = m_drawables[idx].mesh->transform().worldMatrix();
        }
        cmdList.updateBufferImmediately(transformDataBuffer, objectTransforms, m_drawables.size() * sizeof(mat4));
        cmdList.bindSet(transformBindingSet, 1);

        for (uint32_t idx = 0; idx < m_drawables.size(); ++idx) {
            auto& drawable = m_drawables[idx];
            cmdList.drawIndexed(*drawable.vertexBuffer, *drawable.indexBuffer, drawable.indexCount, idx);
        };
    };
}
