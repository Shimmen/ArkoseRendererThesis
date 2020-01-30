#include "CameraUniformNode.h"

#include "camera_state.h"

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
            auto& cameraState = frameAllocator.allocate<CameraState>();

            // TODO: Remove this line, it's the model matrix!
            cameraState.world_from_local = mathkit::scale(50, 50, 50);
            cameraState.world_from_local = mathkit::axisAngleMatrix({ 0, 1, 0 }, appState.elapsedTime() * 3.1415f / 2.0f) * cameraState.world_from_local;

            cameraState.view_from_world = fpsCamera.viewMatrix();
            cameraState.projection_from_view = fpsCamera.projectionMatrix();
            cameraState.view_from_local = cameraState.view_from_world * cameraState.world_from_local;
            cameraState.projection_from_local = cameraState.projection_from_view * cameraState.view_from_local;

            commandList.add<CmdUpdateBuffer>(cameraUniformBuffer, &cameraState, sizeof(CameraState));
        };
    };
}
