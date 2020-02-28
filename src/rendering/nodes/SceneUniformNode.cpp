#include "SceneUniformNode.h"

#include "CameraState.h"
#include "LightData.h"

std::string SceneUniformNode::name()
{
    return "scene-uniforms";
}

SceneUniformNode::SceneUniformNode(const Scene& scene)
    : RenderGraphNode(SceneUniformNode::name())
    , m_scene(scene)
{
}

SceneUniformNode::ExecuteCallback SceneUniformNode::constructFrame(Registry& reg) const
{
    const FpsCamera& camera = m_scene.camera();
    const SunLight& light = m_scene.sun();

    Buffer& cameraUniformBuffer = reg.createBuffer(sizeof(CameraState), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    reg.publish("camera", cameraUniformBuffer);

    Buffer& envDataBuffer = reg.createBuffer(sizeof(float), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    reg.publish("environmentData", envDataBuffer);

    Texture& envTexture = m_scene.environmentMap().empty()
        ? reg.createPixelTexture(vec4(1.0f), true)
        : reg.loadTexture2D(m_scene.environmentMap(), true, false);
    reg.publish("environmentMap", envTexture);

    Buffer& dirLightBuffer = reg.createBuffer(sizeof(DirectionalLight), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    reg.publish("directionalLight", dirLightBuffer);

    return [&](const AppState& appState, CommandList& cmdList) {
        // Camera uniforms
        mat4 projectionFromView = camera.projectionMatrix();
        mat4 viewFromWorld = camera.viewMatrix();
        CameraState cameraState {
            .projectionFromView = projectionFromView,
            .viewFromProjection = inverse(projectionFromView),
            .viewFromWorld = viewFromWorld,
            .worldFromView = inverse(viewFromWorld),
        };
        cmdList.updateBufferImmediately(cameraUniformBuffer, &cameraState, sizeof(CameraState));

        // Environment mapping uniforms
        float envMultiplier = m_scene.environmentMultiplier();
        cmdList.updateBufferImmediately(envDataBuffer, &envMultiplier, sizeof(envMultiplier));

        // Directional light uniforms
        DirectionalLight dirLightData {
            .colorAndIntensity = { light.color, light.intensity },
            .worldSpaceDirection = normalize(vec4(light.direction, 0.0)),
            .viewSpaceDirection = camera.viewMatrix() * normalize(vec4(m_scene.sun().direction, 0.0)),
            .lightProjectionFromWorld = light.lightProjection()
        };
        cmdList.updateBufferImmediately(dirLightBuffer, &dirLightData, sizeof(DirectionalLight));
    };
}
