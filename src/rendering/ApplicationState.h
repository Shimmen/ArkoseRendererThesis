#pragma once

#include "Resources.h"

class ApplicationState {
public:
    ApplicationState() = default;
    ApplicationState(const Extent2D& windowExtent, double deltaTime, double timeSinceStartup, uint32_t frameIndex)
        : m_frameIndex(frameIndex)
        , m_windowExtent(windowExtent)
        , m_deltaTime(deltaTime)
        , m_timeSinceStartup(timeSinceStartup)

    {
    }

    uint32_t frameIndex() const { return m_frameIndex; }
    const Extent2D& windowExtent() const { return m_windowExtent; }
    double deltaTime() const { return m_deltaTime; }
    double elapsedTime() const { return m_timeSinceStartup; }

    [[nodiscard]] ApplicationState updateWindowExtent(Extent2D& newExtent)
    {
        ApplicationState copy = *this;
        copy.m_windowExtent = newExtent;
        return copy;
    }

private:
    uint32_t m_frameIndex {};
    Extent2D m_windowExtent {};

    double m_deltaTime {};
    double m_timeSinceStartup {};
};
