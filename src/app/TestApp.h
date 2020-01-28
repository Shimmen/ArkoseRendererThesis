#pragma once

#include "rendering/App.h"
#include "rendering/nodes/ForwardRenderNode.h"
#include "utility/FpsCamera.h"

class TestApp : public App {
public:
    void setup(StaticResourceManager&) override;
    void makeRenderGraph(RenderGraph&) override;
    void update(float elapsedTime, float deltaTime) override;

private:
    struct Vertex {
        vec3 position;
        vec3 color;
        vec2 texCoord;
    };

    ForwardRenderNode::Object m_object {};
    ForwardRenderNode::Scene m_scene {};

    FpsCamera m_camera {};
};
