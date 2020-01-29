#pragma once

#include "RenderGraph.h"
#include "StaticResourceManager.h"

class App {
public:
    App() = default;
    virtual ~App() = default;

    virtual void setup(StaticResourceManager&, RenderGraph&) = 0;
    virtual void update(float elapsedTime, float deltaTime) = 0;
};
