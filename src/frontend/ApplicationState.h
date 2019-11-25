#pragma once

#include "Resources.h"

struct ApplicationState {
    const int frameIndex;

    const double deltaTime;
    const double timeSinceStartup;

    const Extent2D windowExtent;
    const bool windowSizeDidChange;
};
