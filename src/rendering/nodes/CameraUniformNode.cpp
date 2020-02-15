#include "CameraUniformNode.h"

#include "CameraState.h"

std::string CameraUniformNode::name()
{
    return "camera-uniform";
}

NEWBasicRenderGraphNode::ConstructorFunction CameraUniformNode::construct(const FpsCamera& fpsCamera)
{
    return [&](ResourceManager& frameManager) {
        Buffer& cameraUniformBuffer = frameManager.createBuffer(sizeof(CameraState), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
        frameManager.publish("buffer", cameraUniformBuffer);

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
