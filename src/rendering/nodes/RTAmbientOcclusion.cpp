#include "RTAmbientOcclusion.h"

#include "ForwardRenderNode.h"
#include "RTAccelerationStructures.h"
#include "SceneUniformNode.h"
#include <imgui.h>

RTAmbientOcclusion::RTAmbientOcclusion(const Scene&)
    : RenderGraphNode(RTAmbientOcclusion::name())
{
}

std::string RTAmbientOcclusion::name()
{
    return "rt-ambient-occlusion";
}

RenderGraphNode::ExecuteCallback RTAmbientOcclusion::constructFrame(Registry& reg) const
{
    const Texture* gBufferNormal = reg.getTexture(ForwardRenderNode::name(), "normal");
    const Texture* gBufferDepth = reg.getTexture(ForwardRenderNode::name(), "depth");
    ASSERT(gBufferNormal && gBufferDepth);

    Texture& ambientOcclusion = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::R16F, Texture::Usage::StorageAndSample);
    reg.publish("AO", ambientOcclusion);

    const TopLevelAS& tlas = *reg.getTopLevelAccelerationStructure(RTAccelerationStructures::name(), "scene");
    BindingSet& frameBindingSet = reg.createBindingSet({ { 0, ShaderStageRTRayGen, &tlas },
                                                         { 1, ShaderStageRTRayGen, reg.getBuffer(SceneUniformNode::name(), "camera") },
                                                         { 2, ShaderStageRTRayGen, &ambientOcclusion, ShaderBindingType::StorageImage },
                                                         { 3, ShaderStageRTRayGen, gBufferNormal, ShaderBindingType::TextureSampler },
                                                         { 4, ShaderStageRTRayGen, gBufferDepth, ShaderBindingType::TextureSampler } });

    ShaderFile raygen("rt-ao/raygen.rgen");
    ShaderFile miss("rt-ao/miss.rmiss");
    HitGroup triangleHitGroup(ShaderFile("rt-ao/closestHit.rchit"));
    ShaderBindingTable sbt { raygen, { triangleHitGroup }, { miss } };

    uint32_t maxRecursionDepth = 1;
    RayTracingState& rtState = reg.createRayTracingState(sbt, { &frameBindingSet }, maxRecursionDepth);

    return [&](const AppState& appState, CommandList& cmdList) {
        static bool enabled = true;
        static float radius = 0.25f;
        static int signedNumSamples = 8;
        if (ImGui::CollapsingHeader("RT AO")) {
            ImGui::Checkbox("Enabled", &enabled);
            ImGui::SliderInt("Sample count", &signedNumSamples, 1, 32);
            ImGui::SliderFloat("Max radius", &radius, 0.01f, 2.0f);
        }

        if (!enabled) {
            cmdList.clearTexture(ambientOcclusion, ClearColor(1, 1, 1));
            return;
        }

        cmdList.setRayTracingState(rtState);
        cmdList.bindSet(frameBindingSet, 0);
        cmdList.pushConstant(ShaderStageRTRayGen, radius, 0);
        cmdList.pushConstant(ShaderStageRTRayGen, static_cast<uint32_t>(signedNumSamples), 4);
        cmdList.pushConstant(ShaderStageRTRayGen, appState.frameIndex(), 8);
        cmdList.traceRays(appState.windowExtent());
    };
}
