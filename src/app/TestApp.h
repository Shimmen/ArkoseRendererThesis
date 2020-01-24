#pragma once

#include "rendering/App.h"
#include "rendering/nodes/ForwardRendererNode.h"
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

    ForwardRendererNode::Object m_object {};
    ForwardRendererNode::Scene m_scene {};

    //Texture* m_testTexture {};
    //Buffer* m_vertexBuffer {};
    //Buffer* m_indexBuffer {};
    //size_t m_indexCount {};

    FpsCamera m_camera {};
};
