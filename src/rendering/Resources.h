#pragma once

#include "utility/Badge.h"
#include "utility/copying.h"
#include "utility/mathkit.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class Backend;
class ResourceManager;

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

struct Resource {
public:
    [[nodiscard]] uint64_t id() const;
    void unregisterBackend(Badge<Backend>) const;
    void registerBackend(Badge<Backend>, uint64_t id) const;

    static constexpr uint64_t NullId = UINT64_MAX;

private:
    mutable uint64_t m_id { NullId };
};

struct Texture2D : public Resource {

    enum class Format {
        RGBA8,
        Depth32F
    };

    enum class MinFilter {
        Linear,
        Nearest,
    };

    enum class MagFilter {
        Linear,
        Nearest,
    };

    Texture2D() = default;
    Texture2D(const Texture2D&) = default;
    Texture2D(Badge<ResourceManager>, int width, int height, Format, MinFilter, MagFilter);

    [[nodiscard]] const Extent2D& extent() const { return m_extent; }
    [[nodiscard]] Format format() const { return m_format; }
    [[nodiscard]] MinFilter minFilter() const { return m_minFilter; }
    [[nodiscard]] MagFilter magFilter() const { return m_magFilter; }
    [[nodiscard]] bool hasMipmaps() const;

private:
    Extent2D m_extent;
    Format m_format;
    MinFilter m_minFilter;
    MagFilter m_magFilter;
    bool m_mipmaps;
};

struct RenderTarget : public Resource {

    enum class AttachmentType : unsigned int {
        Color0 = 0,
        Color1 = 1,
        Color2 = 2,
        Color3 = 3,
        Depth = UINT_MAX
    };

    struct Attachment {
        AttachmentType type;
        Texture2D texture;
    };

    RenderTarget() = default;
    RenderTarget(const RenderTarget&) = default;
    explicit RenderTarget(Badge<ResourceManager>);
    explicit RenderTarget(Badge<ResourceManager>, Texture2D& colorTexture);
    explicit RenderTarget(Badge<ResourceManager>, std::initializer_list<Attachment> attachments);

    [[nodiscard]] const Extent2D& extent() const;
    [[nodiscard]] size_t colorAttachmentCount() const;
    [[nodiscard]] size_t totalAttachmentCount() const;
    [[nodiscard]] bool hasDepthAttachment() const;
    [[nodiscard]] bool isWindowTarget() const;

    [[nodiscard]] const std::vector<Attachment>& sortedAttachments() const { return m_attachments; }

private:
    std::vector<Attachment> m_attachments {};
    bool m_isWindowTarget { false };
};

struct Buffer : public Resource {

    // TODO: Remove at some later date, this is just for testing!
    friend class VulkanBackend;

    enum class Usage {
        Vertex,
        Index,
        UniformBuffer,
    };

    enum class MemoryHint {
        TransferOptimal,
        GpuOptimal,
    };

    Buffer() = default;
    Buffer(const Buffer&) = default;
    Buffer(Badge<ResourceManager>, size_t size, Usage usage, MemoryHint memoryHint = MemoryHint::GpuOptimal);

    size_t size() const { return m_size; }
    Usage usage() const { return m_usage; }
    MemoryHint memoryHint() const { return m_memoryHint; }

private:
    size_t m_size { 0 };
    Usage m_usage { Usage::Vertex };
    MemoryHint m_memoryHint { MemoryHint::GpuOptimal };
};
