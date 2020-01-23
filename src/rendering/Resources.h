#pragma once

#include "Shader.h"
#include "utility/Badge.h"
#include "utility/copying.h"
#include "utility/mathkit.h"
#include "utility/util.h"
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

struct Resource {
public:
    [[nodiscard]] uint64_t id() const;
    [[nodiscard]] bool hasBackend() const;
    void unregisterBackend(Badge<Backend>) const;
    void registerBackend(Badge<Backend>, uint64_t id) const;

    static constexpr uint64_t NullId = UINT64_MAX;

private:
    mutable uint64_t m_id { NullId };
};

struct Texture : public Resource {

    enum class Format {
        Unknown,
        RGB8,
        RGBA8,
        sRGB8,
        sRGBA8,
        Depth32F
    };

    enum class Usage {
        Attachment,
        Sampled,
        All,
    };

    enum class MinFilter {
        Linear,
        Nearest,
    };

    enum class MagFilter {
        Linear,
        Nearest,
    };

    Texture() = default;
    Texture(const Texture&) = default;
    Texture(Badge<ResourceManager>, Extent2D, Format, Usage, MinFilter, MagFilter);

    [[nodiscard]] const Extent2D& extent() const { return m_extent; }
    [[nodiscard]] Format format() const { return m_format; }
    [[nodiscard]] Usage usage() const { return m_usage; }
    [[nodiscard]] MinFilter minFilter() const { return m_minFilter; }
    [[nodiscard]] MagFilter magFilter() const { return m_magFilter; }
    [[nodiscard]] bool hasMipmaps() const;

    [[nodiscard]] bool hasDepthFormat() const
    {
        return m_format == Format::Depth32F;
    }

private:
    Extent2D m_extent;
    Format m_format;
    Usage m_usage;
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
        Texture* texture;
    };

    RenderTarget() = default;
    RenderTarget(const RenderTarget&) = default;
    explicit RenderTarget(Badge<ResourceManager>, Texture& colorTexture);
    explicit RenderTarget(Badge<ResourceManager>, std::initializer_list<Attachment> attachments);

    [[nodiscard]] const Extent2D& extent() const;
    [[nodiscard]] size_t colorAttachmentCount() const;
    [[nodiscard]] size_t totalAttachmentCount() const;
    [[nodiscard]] bool hasDepthAttachment() const;

    [[nodiscard]] const Texture* attachment(AttachmentType) const;

    [[nodiscard]] const std::vector<Attachment>& sortedAttachments() const;

private:
    std::vector<Attachment> m_attachments {};
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
        GpuOnly,
    };

    Buffer() = default;
    Buffer(const Buffer&) = default;
    Buffer(Badge<ResourceManager>, size_t size, Usage usage, MemoryHint);

    size_t size() const { return m_size; }
    Usage usage() const { return m_usage; }
    MemoryHint memoryHint() const { return m_memoryHint; }

private:
    size_t m_size { 0 };
    Usage m_usage { Usage::Vertex };
    MemoryHint m_memoryHint { MemoryHint::GpuOptimal };
};

enum class VertexAttributeType {
    Float2,
    Float3,
    Float4
};

struct VertexAttribute {
    uint32_t location {};
    VertexAttributeType type {};
    size_t memoryOffset {};
};

struct VertexLayout {
    size_t vertexStride {};
    std::vector<VertexAttribute> attributes {};
};

struct BlendState {
    bool enabled { false };
};

enum class TriangleWindingOrder {
    Clockwise,
    CounterClockwise
};

enum class PolygonMode {
    Filled,
    Lines,
    Points
};

struct RasterState {
    bool backfaceCullingEnabled { true };
    TriangleWindingOrder frontFace { TriangleWindingOrder::CounterClockwise };
    PolygonMode polygonMode { PolygonMode::Filled };
};

struct Viewport {
    float x { 0.0f };
    float y { 0.0f };
    Extent2D extent;
};

enum class ShaderBindingType {
    UniformBuffer,
    TextureSampler,
};

struct ShaderBinding {

    ShaderBinding(uint32_t index, ShaderStage, const Buffer*);
    ShaderBinding(uint32_t index, ShaderStage, const Texture*);

    uint32_t bindingIndex;
    ShaderStage shaderStage; // TODO: Later we want flags here I guess, so we can have multiple of them..

    ShaderBindingType type;
    const Buffer* buffer;
    const Texture* texture;
};

struct ShaderBindingSet {
    ShaderBindingSet(std::initializer_list<ShaderBinding>);

    const std::vector<ShaderBinding>& shaderBindings() const;

private:
    std::vector<ShaderBinding> m_shaderBindings {};
};

struct RenderState : public Resource {
public:
    RenderState(Badge<ResourceManager>,
        const RenderTarget& renderTarget, VertexLayout vertexLayout,
        Shader shader, ShaderBindingSet shaderBindingSet,
        Viewport viewport, BlendState blendState, RasterState rasterState)
        : m_renderTarget(renderTarget)
        , m_vertexLayout(vertexLayout)
        , m_shader(shader)
        , m_shaderBindingSet(shaderBindingSet)
        , m_viewport(viewport)
        , m_blendState(blendState)
        , m_rasterState(rasterState)
    {
        ASSERT(shader.type() == ShaderType::Raster);
    }

    const RenderTarget& renderTarget() const { return m_renderTarget; }
    const VertexLayout& vertexLayout() const { return m_vertexLayout; }

    const Shader& shader() const { return m_shader; }
    const ShaderBindingSet& shaderBindingSet() const { return m_shaderBindingSet; }

    const Viewport& fixedViewport() const { return m_viewport; }
    const BlendState& blendState() const { return m_blendState; }
    const RasterState& rasterState() const { return m_rasterState; }

private:
    const RenderTarget& m_renderTarget;
    VertexLayout m_vertexLayout;

    Shader m_shader;
    ShaderBindingSet m_shaderBindingSet;

    Viewport m_viewport;
    BlendState m_blendState;
    RasterState m_rasterState;
};
