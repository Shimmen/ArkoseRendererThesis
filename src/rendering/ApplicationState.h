#pragma once

#include "Resources.h"

struct ApplicationState {

    ApplicationState() = default;
    ApplicationState(const Extent2D& windowExtent, bool windowSizeDidChange, double deltaTime, double timeSinceStartup, unsigned int frameIndex)
        : windowExtent(windowExtent)
        , windowSizeDidChange(windowSizeDidChange)
        , deltaTime(deltaTime)
        , timeSinceStartup(timeSinceStartup)
        , frameIndex(frameIndex)
    {
    }

    const unsigned int frameIndex {};

    const double deltaTime {};
    const double timeSinceStartup {};

    const Extent2D windowExtent {};
    const bool windowSizeDidChange {};
};
