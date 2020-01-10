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

    //! Called exactly once, which implies that the actual pipeline is fixed throughout the lifetime of the app
    //! However, note that each render pass doesn't need to be the same per frame, since they can make different command
    virtual RenderGraph createPipeline(const ApplicationState&) = 0;
};
