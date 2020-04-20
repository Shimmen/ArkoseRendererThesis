#include "FinalPostFxNode.h"

#include "ForwardRenderNode.h"
#include "RTAmbientOcclusion.h"
#include "RTDiffuseGINode.h"
#include "RTFirstHitNode.h"
#include "RTReflectionsNode.h"
#include "SceneUniformNode.h"
#include "imgui.h"

FinalPostFxNode::FinalPostFxNode(const Scene& scene)
    : RenderGraphNode(FinalPostFxNode::name())
    , m_scene(scene)
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
        sourceTexture = &reg.loadTexture2D("assets/test-pattern.png", true, true);
    }

    if (!sourceTextureRt) {
        sourceTextureRt = &reg.loadTexture2D("assets/test-pattern.png", true, true);
    }

    BindingSet& sourceImage = reg.createBindingSet({ { 0, ShaderStageFragment, sourceTexture } });
    BindingSet& sourceImageRt = reg.createBindingSet({ { 0, ShaderStageFragment, sourceTextureRt } });

    // TODO: Maybe return an optional instead, so we can use value_or(..)
    const Texture* diffuseGI = reg.getTexture(RTDiffuseGINode::name(), "diffuseGI");
    if (!diffuseGI) {
        diffuseGI = &reg.createPixelTexture(vec4(0, 0, 0, 1), true);
    }
    const Texture* ambientOcclusion = reg.getTexture(RTAmbientOcclusion::name(), "AO");
    if (!ambientOcclusion) {
        ambientOcclusion = &reg.createPixelTexture(vec4(1, 1, 1, 1), true);
    }
    BindingSet& etcBindingSet = reg.createBindingSet({ { 0, ShaderStageFragment, diffuseGI },
                                                       { 1, ShaderStageFragment, ambientOcclusion } });

    BindingSet& envBindingSet = reg.createBindingSet({ { 0, ShaderStageVertex, reg.getBuffer(SceneUniformNode::name(), "camera") },
                                                       { 1, ShaderStageFragment, reg.getTexture(SceneUniformNode::name(), "environmentMap") },
                                                       { 2, ShaderStageFragment, reg.getTexture(ForwardRenderNode::name(), "depth") },
                                                       { 3, ShaderStageFragment, reg.getBuffer(SceneUniformNode::name(), "environmentData") } });

    RenderStateBuilder renderStateBuilder { reg.windowRenderTarget(), shader, vertexLayout };
    renderStateBuilder.addBindingSet(sourceImage).addBindingSet(sourceImageRt).addBindingSet(etcBindingSet).addBindingSet(envBindingSet);
    renderStateBuilder.writeDepth = false;
    renderStateBuilder.testDepth = false;

    RenderState& renderState = reg.createRenderState(renderStateBuilder);

    return [&](const AppState& appState, CommandList& cmdList) {
        static bool useRtFirstHit = false;
        static bool includeDiffuseGI = true;
        if (ImGui::CollapsingHeader("Final PostFX")) {
            ImGui::Checkbox("Use ray traced first-hit", &useRtFirstHit);
            ImGui::Checkbox("Include diffuse GI", &includeDiffuseGI);
        }

        cmdList.setRenderState(renderState, ClearColor(0.5f, 0.1f, 0.5f), 1.0f);
        cmdList.bindSet(useRtFirstHit ? sourceImageRt : sourceImage, 0);
        cmdList.bindSet(etcBindingSet, 1);
        cmdList.bindSet(envBindingSet, 2);

        cmdList.pushConstant(ShaderStageFragment, includeDiffuseGI);

        cmdList.draw(vertexBuffer, 3);
    };
}
