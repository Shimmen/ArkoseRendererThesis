#pragma once

#include "Shader.h"
#include "utility/Badge.h"
#include "utility/Extent.h"
#include "utility/mathkit.h"
#include "utility/util.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class Backend;
class Registry;

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

struct ClearColor {
    ClearColor(float rgb[3], float a = 1.0f)
        : r(pow(rgb[0], 2.2f))
        , g(pow(rgb[1], 2.2f))
        , b(pow(rgb[2], 2.2f))
        , a(a)
    {
    }
    ClearColor(float r, float g, float b, float a = 1.0f)
        : r(pow(r, 2.2f))
        , g(pow(g, 2.2f))
        , b(pow(b, 2.2f))
        , a(a)
    {
    }

    float r { 0.0f };
    float g { 0.0f };
    float b { 0.0f };
    float a { 0.0f };
};

struct Texture : public Resource {

    enum class Format {
        Unknown,
        RGB8,
        RGBA8,
        sRGBA8,
        RGBA16F,
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

    enum class Mipmap {
        None,
        Nearest,
        Linear,
    };

    Texture() = default;
    Texture(const Texture&) = default;
    Texture(Badge<Registry>, Extent2D, Format, Usage, MinFilter, MagFilter, Mipmap);

    [[nodiscard]] const Extent2D& extent() const { return m_extent; }
    [[nodiscard]] Format format() const { return m_format; }
    [[nodiscard]] Usage usage() const { return m_usage; }
    [[nodiscard]] MinFilter minFilter() const { return m_minFilter; }
    [[nodiscard]] MagFilter magFilter() const { return m_magFilter; }

    [[nodiscard]] Mipmap mipmap() const { return m_mipmap; }
    [[nodiscard]] bool hasMipmaps() const;
    [[nodiscard]] uint32_t mipLevels() const;

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
    Mipmap m_mipmap;
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
    explicit RenderTarget(Badge<Registry>, Texture& colorTexture);
    explicit RenderTarget(Badge<Registry>, std::initializer_list<Attachment> attachments);

    [[nodiscard]] const Extent2D& extent() const;
    [[nodiscard]] size_t colorAttachmentCount() const;
    [[nodiscard]] size_t totalAttachmentCount() const;
    [[nodiscard]] bool hasDepthAttachment() const;

    [[nodiscard]] const Texture* attachment(AttachmentType) const;

    [[nodiscard]] const std::vector<Attachment>& sortedAttachments() const;

    void forEachColorAttachment(std::function<void(const Attachment&)>) const;

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
    Buffer(Badge<Registry>, size_t size, Usage usage, MemoryHint);

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

enum ShaderStage : uint8_t {
    ShaderStageVertex = 0x01,
    ShaderStageFragment = 0x02,
    ShaderStageCompute = 0x04,
};

enum class ShaderBindingType {
    UniformBuffer,
    TextureSampler,
    TextureSamplerArray,
};

struct ShaderBinding {

    ShaderBinding(uint32_t index, ShaderStage, const Buffer*);
    ShaderBinding(uint32_t index, ShaderStage, const Texture*);
    ShaderBinding(uint32_t index, ShaderStage, const std::vector<const Texture*>&, uint32_t count);

    uint32_t bindingIndex;
    uint32_t count;

    ShaderStage shaderStage; // TODO: Later we want flags here I guess, so we can have multiple of them..

    ShaderBindingType type;
    const Buffer* buffer;
    std::vector<const Texture*> textures;
};

struct BindingSet : public Resource {
    BindingSet(Badge<Registry>, std::vector<ShaderBinding>);

    const std::vector<ShaderBinding>& shaderBindings() const;

private:
    std::vector<ShaderBinding> m_shaderBindings {};
};

struct RenderState : public Resource {
public:
    RenderState(Badge<Registry>,
                const RenderTarget& renderTarget, VertexLayout vertexLayout,
                Shader shader, const std::vector<const BindingSet*>& shaderBindingSets,
                Viewport viewport, BlendState blendState, RasterState rasterState)
        : m_renderTarget(renderTarget)
        , m_vertexLayout(vertexLayout)
        , m_shader(shader)
        , m_shaderBindingSets(shaderBindingSets)
        , m_viewport(viewport)
        , m_blendState(blendState)
        , m_rasterState(rasterState)
    {
        ASSERT(shader.type() == ShaderType::Raster);
    }

    const RenderTarget& renderTarget() const { return m_renderTarget; }
    const VertexLayout& vertexLayout() const { return m_vertexLayout; }

    const Shader& shader() const { return m_shader; }
    const std::vector<const BindingSet*>& bindingSets() const { return m_shaderBindingSets; }

    const Viewport& fixedViewport() const { return m_viewport; }
    const BlendState& blendState() const { return m_blendState; }
    const RasterState& rasterState() const { return m_rasterState; }

private:
    const RenderTarget& m_renderTarget;
    VertexLayout m_vertexLayout;

    Shader m_shader;
    std::vector<const BindingSet*> m_shaderBindingSets;

    Viewport m_viewport;
    BlendState m_blendState;
    RasterState m_rasterState;
};

class RenderStateBuilder {
public:
    RenderStateBuilder(const RenderTarget&, const Shader&, const VertexLayout&);

    const RenderTarget& renderTarget;
    const VertexLayout& vertexLayout;
    const Shader& shader;

    [[nodiscard]] Viewport viewport() const;
    [[nodiscard]] BlendState blendState() const;
    [[nodiscard]] RasterState rasterState() const;

    RenderStateBuilder& addBindingSet(const BindingSet&);
    [[nodiscard]] const std::vector<const BindingSet*>& bindingSets() const;

private:
    std::optional<Viewport> m_viewport {};
    std::optional<BlendState> m_blendState {};
    std::optional<RasterState> m_rasterState {};
    std::vector<const BindingSet*> m_bindingSets {};
};
