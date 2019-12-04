#pragma once

#include "ApplicationState.h"
#include "GpuPipeline.h"
#include "RenderPass.h"

class ResourceManager;

class App {
public:
    explicit App(ResourceManager&);

    // Maybe these could be called by main and only the result of createPipeline is passed to the backend?
    void setup(const ApplicationState&);
    void timeStepForFrame(const ApplicationState&);

    //! Called exactly once, which implies that the actual pipeline is fixed throughout the lifetime of the app
    //! However, note that each render pass doesn't need to be the same per frame, since they can make different command
    GpuPipeline createPipeline(const ApplicationState&);

private:
    ResourceManager& m_resourceManager;

    // FIXME: App subclass specific stuff below
    struct Vertex {
        vec3 position;
        vec3 color;
        vec2 texCoord;
    };
    VertexLayout m_vertexLayout {};
    Buffer m_vertexBuffer {};
    Buffer m_indexBuffer {};
    size_t m_indexCount {};
    Shader m_shader {};
};
