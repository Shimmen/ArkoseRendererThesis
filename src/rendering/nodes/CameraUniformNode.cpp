#include "CameraUniformNode.h"

#include "CameraState.h"

std::string CameraUniformNode::name()
{
    return "camera-uniform";
}

RenderGraphNode::NodeConstructorFunction CameraUniformNode::construct(const FpsCamera& fpsCamera)
{
    return [&](Registry& registry) {
        Buffer& cameraUniformBuffer = registry.frame.createBuffer(sizeof(CameraState), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
        registry.frame.publish("buffer", cameraUniformBuffer);

        return [&](const AppState& appState, CommandList& cmdList) {
            CameraState cameraState {
                .viewFromWorld = fpsCamera.viewMatrix(),
                .worldFromView = inverse(fpsCamera.viewMatrix()),
                .projectionFromView = fpsCamera.projectionMatrix()
            };
            cmdList.updateBufferImmediately(cameraUniformBuffer, &cameraState, sizeof(CameraState));
        };
    };
}
