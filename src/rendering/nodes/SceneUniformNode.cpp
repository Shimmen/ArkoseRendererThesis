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

    // TODO: This is all temporary hacking about to get a spot light in now..
    Buffer& spotLightBuffer = reg.createBuffer(sizeof(SpotLightData), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    reg.publish("spotLight", spotLightBuffer);

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
        const SunLight& sunLight = m_scene.sun();
        DirectionalLight dirLightData {
            .colorAndIntensity = { sunLight.color, sunLight.intensity },
            .worldSpaceDirection = normalize(vec4(sunLight.direction, 0.0)),
            .viewSpaceDirection = camera.viewMatrix() * normalize(vec4(sunLight.direction, 0.0)),
            .lightProjectionFromWorld = sunLight.lightProjection()
        };
        cmdList.updateBufferImmediately(dirLightBuffer, &dirLightData, sizeof(DirectionalLight));

        // Splot light light uniforms
        if (!m_scene.spotLights().empty()) {
            const SpotLight& spotLight = m_scene.spotLights().front();
            SpotLightData spotLightData {
                .colorAndIntensity = { spotLight.color, spotLight.intensity },
                .worldSpacePosition = vec4(spotLight.position, 1.0f),
                .worldSpaceDirection = vec4(normalize(spotLight.direction), 0.0f),
                .viewSpacePosition = camera.viewMatrix() * vec4(spotLight.position, 1.0f),
                .viewSpaceDirection = camera.viewMatrix() * vec4(normalize(spotLight.direction), 0.0f),
                .lightProjectionFromWorld = spotLight.lightProjection(),
                .coneAngle = spotLight.coneAngle
            };
            cmdList.updateBufferImmediately(spotLightBuffer, &spotLightData, sizeof(SpotLightData));
        }
    };
}
