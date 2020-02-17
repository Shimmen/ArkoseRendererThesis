#include "CameraUniformNode.h"

#include "CameraState.h"

std::string CameraUniformNode::name()
{
    return "camera-uniform";
}

CameraUniformNode::CameraUniformNode(const FpsCamera& fpsCamera)
    : RenderGraphNode(CameraUniformNode::name())
    , m_fpsCamera(&fpsCamera)
{
}

CameraUniformNode::ExecuteCallback CameraUniformNode::constructFrame(Registry& reg) const
{
    Buffer& cameraUniformBuffer = reg.createBuffer(sizeof(CameraState), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    reg.publish("buffer", cameraUniformBuffer);

    return [&](const AppState& appState, CommandList& cmdList) {
        CameraState cameraState {
            .viewFromWorld = m_fpsCamera->viewMatrix(),
            .worldFromView = inverse(m_fpsCamera->viewMatrix()),
            .projectionFromView = m_fpsCamera->projectionMatrix()
        };
        cmdList.updateBufferImmediately(cameraUniformBuffer, &cameraState, sizeof(CameraState));
    };
}
