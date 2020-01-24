#pragma once

#include "Badge.h"
#include "Extent.h"

class Backend;

class GlobalState {
public:
    static const GlobalState& get();
    static GlobalState& getMutable(Badge<Backend>);

    Extent2D windowExtent() const;
    void updateWindowExtent(Extent2D);

private:
    Extent2D m_windowExtent {};
};
