#include "FinalPostFxNode.h"

#include "ForwardRenderNode.h"
#include "RTFirstHitNode.h"
#include "imgui.h"

FinalPostFxNode::FinalPostFxNode()
    : RenderGraphNode(FinalPostFxNode::name())
{
}

std::string FinalPostFxNode::name()
{
    return "final";
}

FinalPostFxNode::ExecuteCallback FinalPostFxNode::constructFrame(Registry& reg) const
{
    Shader shader = Shader::createBasic("finalPostFx.vert", "finalPostFx.frag");

    VertexLayout vertexLayout = VertexLayout { sizeof(vec2), { { 0, VertexAttributeType::Float2, 0 } } };
    std::vector<vec2> fullScreenTriangle { { -1, -3 }, { -1, 1 }, { 3, 1 } };
    Buffer& vertexBuffer = reg.createBuffer(std::move(fullScreenTriangle), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);

    const Texture* sourceTexture = reg.getTexture(ForwardRenderNode::name(), "color");
    const Texture* sourceTextureRt = reg.getTexture(RTFirstHitNode::name(), "image");

    if (!sourceTexture) {
        LogError("FinalPostFxNode: could not find the input texture 'forward:color', using test texture\n");
        sourceTexture = &reg.loadTexture2D("assets/test-pattern.png", true, true);
    }

    BindingSet& bindingSet = reg.createBindingSet({ { 0, ShaderStageFragment, sourceTexture } });
    BindingSet& bindingSetRt = reg.createBindingSet({ { 0, ShaderStageFragment, sourceTextureRt } });

    RenderStateBuilder renderStateBuilder { reg.windowRenderTarget(), shader, vertexLayout };
    renderStateBuilder.addBindingSet(bindingSet);
    renderStateBuilder.addBindingSet(bindingSetRt);
    RenderState& renderState = reg.createRenderState(renderStateBuilder);

    return [&](const AppState& appState, CommandList& cmdList) {
        static bool useRt = false;
        if (ImGui::CollapsingHeader("Final PostFX")) {
            ImGui::Checkbox("Use ray traced results", &useRt);
        }

        cmdList.setRenderState(renderState, ClearColor(0.5f, 0.1f, 0.5f), 1.0f);
        cmdList.bindSet(useRt ? bindingSetRt : bindingSet, 0);
        cmdList.draw(vertexBuffer, 3);
    };
}
