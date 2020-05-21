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
    Buffer& transformDataBuffer = reg.createBuffer(m_drawables.size() * sizeof(mat4), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    BindingSet& transformBindingSet = reg.createBindingSet({ { 0, ShaderStageVertex, &transformDataBuffer } });

    Shader shader = Shader::createVertexOnly("light/shadow.vert");
    VertexLayout vertexLayout = VertexLayout { sizeof(vec3), { { 0, VertexAttributeType::Float3, 0 } } };

    struct DrawContext {
        const RenderState* renderState {};
        const Light* light {};
    };
    std::vector<DrawContext> drawContexts;

    m_scene.forEachLight([&](const Light& light) {
        if (!light.shadowMap.has_value()) {
            return;
        }

        const ShadowMapSpec& mapSpec = light.shadowMap.value();

        Texture& shadowMap = reg.createTexture2D(mapSpec.size, Texture::Format::Depth32F, Texture::Usage::AttachAndSample);
        reg.publish(mapSpec.name, shadowMap);

        const RenderTarget& shadowRenderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Depth, &shadowMap } });
        RenderStateBuilder renderStateBuilder { shadowRenderTarget, shader, vertexLayout };
        renderStateBuilder.addBindingSet(transformBindingSet);
        RenderState& renderState = reg.createRenderState(renderStateBuilder);

        DrawContext ctx;
        ctx.light = &light;
        ctx.renderState = &renderState;
        drawContexts.push_back(ctx);
    });

    return [&, drawContexts = drawContexts](const AppState& appState, CommandList& cmdList) {
        mat4 objectTransforms[SHADOW_MAX_OCCLUDERS];
        for (uint32_t idx = 0; idx < m_drawables.size(); ++idx) {
            objectTransforms[idx] = m_drawables[idx].mesh->transform().worldMatrix();
        }
        cmdList.updateBufferImmediately(transformDataBuffer, objectTransforms, m_drawables.size() * sizeof(mat4));

        for (const DrawContext& ctx : drawContexts) {

            cmdList.setRenderState(*ctx.renderState, ClearColor(1, 0, 1), 1.0f);
            cmdList.pushConstant(ShaderStageVertex, ctx.light->lightProjection());
            cmdList.bindSet(transformBindingSet, 0);

            for (uint32_t idx = 0; idx < m_drawables.size(); ++idx) {
                auto& drawable = m_drawables[idx];
                cmdList.drawIndexed(*drawable.vertexBuffer, *drawable.indexBuffer, drawable.indexCount, drawable.mesh->indexType(), idx);
            };
        }
    };
}
