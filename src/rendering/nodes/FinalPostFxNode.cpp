#include "FinalPostFxNode.h"

#include "ForwardRenderNode.h"

std::string FinalPostFxNode::name()
{
    return "final";
}

RenderGraphNode::NodeConstructorFunction FinalPostFxNode::construct()
{
    return [&](Registry& registry) {
        Shader shader = Shader::createBasic("finalPostFX", "finalPostFx.vert", "finalPostFx.frag");

        VertexLayout vertexLayout = VertexLayout { sizeof(vec2), { { 0, VertexAttributeType::Float2, 0 } } };
        std::vector<vec2> fullScreenTriangle { { -1, -3 }, { -1, 1 }, { 3, 1 } };
        Buffer& vertexBuffer = registry.frame.createBuffer(std::move(fullScreenTriangle), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);

        const Texture* sourceTexture = registry.frame.getTexture(ForwardRenderNode::name(), "color");
        if (!sourceTexture) {
            LogError("FinalPostFxNode: could not find the input texture 'forward:color', using test texture\n");
            //sourceTexture = &registry.node.loadTexture2D("assets/test-pattern.png", true, true);
            sourceTexture = &registry.frame.loadTexture2D("assets/test-pattern.png", true, true);
        }

        ShaderBinding textureSamplerBinding = { 0, ShaderStageFragment, sourceTexture };
        BindingSet& bindingSet = registry.frame.createBindingSet({ textureSamplerBinding });

        const RenderTarget& windowTarget = registry.frame.windowRenderTarget();

        Viewport viewport;
        viewport.extent = windowTarget.extent();

        BlendState blendState;
        blendState.enabled = false;

        RasterState rasterState;
        rasterState.polygonMode = PolygonMode::Filled;
        rasterState.backfaceCullingEnabled = true;
        rasterState.frontFace = TriangleWindingOrder::CounterClockwise;

        RenderState& renderState = registry.frame.createRenderState(windowTarget, vertexLayout, shader, { &bindingSet }, viewport, blendState, rasterState);

        return [&](const AppState& appState, CommandList& cmdList) {
            cmdList.setRenderState(renderState, ClearColor(0.5f, 0.1f, 0.5f), 1.0f);
            cmdList.bindSet(bindingSet, 0);
            cmdList.draw(vertexBuffer, 3);
        };
    };
}
