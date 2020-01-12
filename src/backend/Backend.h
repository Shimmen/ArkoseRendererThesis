#pragma once

#include "utility/Badge.h"
#include "utility/util.h"
#include <rendering/RenderGraph.h>

class App;
class StaticResourceManager;

class Backend {
public:
    Backend() = default;
    virtual ~Backend() = default;

    virtual void createStaticResources(StaticResourceManager&) = 0;
    virtual void destroyStaticResources(StaticResourceManager&) = 0;

    virtual void setMainRenderGraph(RenderGraph&) = 0;

    virtual bool executeFrame(double elapsedTime, double deltaTime)
        = 0;

protected:
    [[nodiscard]] static Badge<Backend> backendBadge()
    {
        return {};
    }
};
