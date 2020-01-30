#include "CameraUniformNode.h"

#include "CameraState.h"

std::string CameraUniformNode::name()
{
    return "camera-uniform";
}

RenderGraphNode::NodeConstructorFunction CameraUniformNode::construct(const FpsCamera& fpsCamera)
{
    return [&](ResourceManager& resourceManager) {
        Buffer& cameraUniformBuffer = resourceManager.createBuffer(sizeof(CameraState), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
        resourceManager.publish("buffer", cameraUniformBuffer);

        return [&](const AppState& appState, CommandList& commandList, FrameAllocator& frameAllocator) {
            auto& cameraState = frameAllocator.allocateSingle<CameraState>();
            cameraState.viewFromWorld = fpsCamera.viewMatrix();
            cameraState.projectionFromView = fpsCamera.projectionMatrix();
            commandList.add<CmdUpdateBuffer>(cameraUniformBuffer, &cameraState, sizeof(CameraState));
        };
    };
}
