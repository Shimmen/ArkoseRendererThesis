#pragma once

#include "rendering/App.h"

class TestApp : public App {
public:
    void setup(StaticResourceManager&) override;
    void makeRenderGraph(RenderGraph&) override;

private:
    struct Vertex {
        vec3 position;
        vec3 color;
        vec2 texCoord;
    };

    Buffer* m_vertexBuffer {};
    Buffer* m_indexBuffer {};
    size_t m_indexCount {};
};
