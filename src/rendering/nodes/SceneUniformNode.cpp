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
