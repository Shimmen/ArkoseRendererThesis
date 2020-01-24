#pragma once

#include "ApplicationState.h"
#include "RenderGraph.h"
#include "RenderGraphNode.h"
#include "StaticResourceManager.h"

class App {
public:
    App() {}
    virtual ~App() {}

    virtual void setup(StaticResourceManager&) = 0;
    virtual void makeRenderGraph(RenderGraph&) = 0;

    virtual void update(float elapsedTime, float deltaTime) = 0;
};
