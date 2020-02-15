#pragma once

#include "utility/util.h"
#include <cstdint>

struct Extent2D {
    Extent2D()
        : Extent2D(0, 0)
    {
    }
    Extent2D(uint32_t width, uint32_t height)
        : m_width(width)
        , m_height(height)
    {
    }
    Extent2D(int width, int height)
        : m_width(width)
        , m_height(height)
    {
        ASSERT(width >= 0);
        ASSERT(height >= 0);
    }
    Extent2D(const Extent2D& other)
        : Extent2D(other.m_width, other.m_height)
    {
    }

    [[nodiscard]] uint32_t width() const { return m_width; }
    [[nodiscard]] uint32_t height() const { return m_height; }

    bool operator!=(const Extent2D& other) const
    {
        return !(*this == other);
    }
    bool operator==(const Extent2D& other) const
    {
        return m_width == other.m_width && m_height == other.m_height;
    }

private:
    uint32_t m_width {};
    uint32_t m_height {};
};
