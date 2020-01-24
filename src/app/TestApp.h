#pragma once

#include "rendering/App.h"
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

    Texture* m_testTexture {};
    Buffer* m_vertexBuffer {};
    Buffer* m_indexBuffer {};
    size_t m_indexCount {};

    FpsCamera m_camera {};
};
