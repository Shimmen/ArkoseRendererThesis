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
    virtual std::unique_ptr<RenderGraph> mainRenderGraph() = 0;
};
