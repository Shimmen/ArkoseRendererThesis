#pragma once

#include "RenderGraph.h"
#include "ResourceManager.h"

class App {
public:
    App() = default;
    virtual ~App() = default;

    virtual void setup(ResourceManager& staticResources, RenderGraph&) = 0;
    virtual void update(float elapsedTime, float deltaTime) = 0;
};
