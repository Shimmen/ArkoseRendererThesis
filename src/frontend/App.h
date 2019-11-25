#pragma once

#include "ApplicationState.h"
#include "RenderPass.h"
#include "ResourceManager.h"
#include <memory>

class App {
public:
    explicit App(ResourceManager&);

    void setup(ApplicationState);
    void drawFrame(ApplicationState);

private:
    ResourceManager& m_resourceManager;

    // TODO: Move these stuff below to some subclass..?
    Texture2D m_testTexture {};
    Buffer m_vertexBuffer {};
    Buffer m_indexBuffer {};

    std::unique_ptr<RenderPass> m_renderPass {};
};
