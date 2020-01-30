#include "FinalPostFxNode.h"

#include "ForwardRenderNode.h"

std::string FinalPostFxNode::name()
{
    return "final";
}

RenderGraphNode::NodeConstructorFunction FinalPostFxNode::construct()
{
    return [&](ResourceManager& resourceManager) {
        Shader shader = Shader::createBasic("finalPostFX", "finalPostFx.vert", "finalPostFx.frag");

        VertexLayout vertexLayout = VertexLayout { sizeof(vec2), { { 0, VertexAttributeType::Float2, 0 } } };
        std::vector<vec2> fullScreenTriangle { { -1, -3 }, { -1, 1 }, { 3, 1 } };
        Buffer& vertexBuffer = resourceManager.createBuffer(std::move(fullScreenTriangle), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);

        const Texture* sourceTexture = resourceManager.getTexture(ForwardRenderNode::name(), "color");
        if (!sourceTexture) {
            LogError("FinalPostFxNode: could not find the input texture 'forward:color', using test texture\n");
            sourceTexture = &resourceManager.loadTexture2D("assets/test-pattern.png", true, true);
        }

        ShaderBinding textureSamplerBinding = { 0, ShaderStage::Fragment, sourceTexture };
        ShaderBindingSet shaderBindingSet { textureSamplerBinding };

        const RenderTarget& windowTarget = resourceManager.windowRenderTarget();

        Viewport viewport;
        viewport.extent = windowTarget.extent();

        BlendState blendState;
        blendState.enabled = false;

        RasterState rasterState;
        rasterState.polygonMode = PolygonMode::Filled;
        rasterState.backfaceCullingEnabled = true;
        rasterState.frontFace = TriangleWindingOrder::CounterClockwise;

        RenderState& renderState = resourceManager.createRenderState(windowTarget, vertexLayout, shader, shaderBindingSet, viewport, blendState, rasterState);

        return [&](const ApplicationState& appState, CommandList& commandList, FrameAllocator& frameAllocator) {
            commandList.add<CmdSetRenderState>(renderState);
            commandList.add<CmdClear>(ClearColor(0.5f, 0.1f, 0.5f), 1.0f);
            commandList.add<CmdDrawArray>(vertexBuffer, 3, DrawMode::Triangles);
        };
    };
}
