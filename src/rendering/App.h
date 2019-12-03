#pragma once

#include "ApplicationState.h"
#include "RenderPass.h"
#include <memory>

class ResourceManager;

class App {
public:
    explicit App();

    void setup(ApplicationState);
    void drawFrame(ApplicationState);

private:
    // TODO: How exactly should we set this?
    ResourceManager* m_resourceManager;

    // TODO: Move these stuff below to some subclass..?
    //Texture2D m_testTexture {};
    //Buffer m_vertexBuffer {};
    //Buffer m_indexBuffer {};

    std::unique_ptr<RenderPass> m_renderPass {};
};
