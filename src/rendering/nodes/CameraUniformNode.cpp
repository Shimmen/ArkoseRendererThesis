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

        return [&](const AppState& appState, CommandList& commandList) {
            //auto& cameraState = registry.frame.allocator().allocateSingle<CameraState>();
            static CameraState cameraState {};
            cameraState.viewFromWorld = fpsCamera.viewMatrix();
            cameraState.projectionFromView = fpsCamera.projectionMatrix();
            commandList.add<CmdUpdateBuffer>(cameraUniformBuffer, &cameraState, sizeof(CameraState));
        };
    };
}
