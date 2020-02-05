#pragma once

#include "utility/Badge.h"
#include "utility/util.h"

enum class BackendFeature {
    TextureArrayDynamicIndexing
};

class Backend {
public:
    Backend() = default;
    virtual ~Backend() = default;

    virtual bool executeFrame(double elapsedTime, double deltaTime) = 0;

protected:
    [[nodiscard]] static Badge<Backend> backendBadge()
    {
        return {};
    }
};
