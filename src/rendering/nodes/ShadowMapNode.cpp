#include "ShadowMapNode.h"

#include "ShadowData.h"
#include "utility/mathkit.h"

std::string ShadowMapNode::name()
{
    return "shadow";
}

RenderGraphBasicNode::ConstructorFunction ShadowMapNode::construct(const Scene& scene)
{
    return [&](Registry& reg) {
        static std::vector<Drawable> drawables {}; // TODO: Don't use static data like this!
        setupDrawables(scene, reg, drawables); // TODO: Don't use the frame registry!

        Shader shader = Shader::createVertexOnly("shadowSun", "shadowSun.vert");
        VertexLayout vertexLayout = VertexLayout { sizeof(vec3), { { 0, VertexAttributeType::Float3, 0 } } };

        const SunLight& sunLight = scene.sun();

        Texture& shadowMap = reg.createTexture2D(sunLight.shadowMapSize, Texture::Format::Depth32F, Texture::Usage::All);
        reg.publish("directional", shadowMap);

        const RenderTarget& shadowRenderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Depth, &shadowMap } });

        Viewport viewport;
        viewport.extent = shadowRenderTarget.extent();

        BlendState blendState;
        blendState.enabled = false;

        RasterState rasterState;
        rasterState.polygonMode = PolygonMode::Filled;
        rasterState.frontFace = TriangleWindingOrder::CounterClockwise;
        rasterState.backfaceCullingEnabled = true;

        Buffer& lightDataBuffer = reg.createBuffer(sizeof(mat4), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
        BindingSet& lightBindingSet = reg.createBindingSet({ { 0, ShaderStageVertex, &lightDataBuffer } });

        Buffer& transformDataBuffer = reg.createBuffer(drawables.size() * sizeof(mat4), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
        BindingSet& transformBindingSet = reg.createBindingSet({ { 0, ShaderStageVertex, &transformDataBuffer } });

        std::vector<const BindingSet*> allBindingSets { &lightBindingSet, &transformBindingSet };

        RenderState& renderState = reg.createRenderState(shadowRenderTarget, vertexLayout, shader, allBindingSets, viewport, blendState, rasterState);

        return [&](const AppState& appState, CommandList& cmdList) {
            cmdList.setRenderState(renderState, ClearColor(1, 0, 1), 1.0f);

            mat4 lightProjectionFromWorld = sunLight.lightProjection();
            cmdList.updateBufferImmediately(lightDataBuffer, &lightProjectionFromWorld, sizeof(mat4));
            cmdList.bindSet(lightBindingSet, 0);

            mat4 objectTransforms[SHADOW_MAX_OCCLUDERS];
            for (uint32_t idx = 0; idx < drawables.size(); ++idx) {
                objectTransforms[idx] = drawables[idx].mesh->transform().worldMatrix();
            }
            cmdList.updateBufferImmediately(transformDataBuffer, objectTransforms, drawables.size() * sizeof(mat4));
            cmdList.bindSet(transformBindingSet, 1);

            for (uint32_t idx = 0; idx < drawables.size(); ++idx) {
                auto& drawable = drawables[idx];
                cmdList.drawIndexed(*drawable.vertexBuffer, *drawable.indexBuffer, drawable.indexCount, idx);
            };
        };
    };
}

void ShadowMapNode::setupDrawables(const Scene& scene, Registry& resources, std::vector<Drawable>& drawables)
{
    drawables.clear();

    scene.forEachDrawable([&](int, const Mesh& mesh) {
        Drawable drawable {};
        drawable.mesh = &mesh;

        drawable.vertexBuffer = &resources.createBuffer(mesh.positionData(), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);
        drawable.indexBuffer = &resources.createBuffer(mesh.indexData(), Buffer::Usage::Index, Buffer::MemoryHint::GpuOptimal);
        drawable.indexCount = mesh.indexCount();

        drawables.push_back(drawable);
    });
}
