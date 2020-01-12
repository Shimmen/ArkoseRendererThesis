#pragma once

#include "rendering/App.h"

class TestApp : public App {
public:
    void setup(StaticResourceManager&) override;
    std::unique_ptr<RenderGraph> mainRenderGraph() override;

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
