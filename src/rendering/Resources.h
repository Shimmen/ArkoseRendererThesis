#pragma once

#include "Shader.h"
#include "utility/Badge.h"
#include "utility/Extent.h"
#include "utility/Model.h"
#include "utility/mathkit.h"
#include "utility/util.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <variant>
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
        RGBA8,
        sRGBA8,
        RGBA16F,
        RGBA32F,
        Depth32F
    };

    enum class Usage {
        Attachment,
        Sampled,
        AttachAndSample,
        StorageAndSample,
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

enum class LoadOp {
    Clear,
    Load,
};

enum class StoreOp {
    Ignore,
    Store,
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
        AttachmentType type { AttachmentType::Color0 };
        Texture* texture { nullptr };
        LoadOp loadOp { LoadOp::Clear };
        StoreOp storeOp { StoreOp::Store };
    };

    RenderTarget() = default;
    RenderTarget(const RenderTarget&) = default;
    explicit RenderTarget(Badge<Registry>, Texture& colorTexture, LoadOp = LoadOp::Clear, StoreOp = StoreOp::Store);
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

    enum class Usage {
        Vertex,
        Index,
        UniformBuffer,
        StorageBuffer,
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

struct DepthState {
    bool writeDepth { true };
    bool testDepth { true };
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

enum class PipelineStage {
    Host,
    RayTracing,
    // TODO: Add more obviously
};

enum ShaderStage : uint8_t {
    ShaderStageVertex = 0x01,
    ShaderStageFragment = 0x02,
    ShaderStageCompute = 0x04,
    ShaderStageRTRayGen = 0x08,
    ShaderStageRTMiss = 0x10,
    ShaderStageRTClosestHit = 0x20,
};

enum class ShaderBindingType {
    UniformBuffer,
    StorageBuffer,
    StorageImage,
    TextureSampler,
    TextureSamplerArray,
    StorageBufferArray,
    RTAccelerationStructure,
};

class TopLevelAS;

struct ShaderBinding {

    // Single uniform or storage buffer
    ShaderBinding(uint32_t index, ShaderStage, const Buffer*, ShaderBindingType = ShaderBindingType::UniformBuffer);

    // Single sampled texture or storage image
    ShaderBinding(uint32_t index, ShaderStage, const Texture*, ShaderBindingType = ShaderBindingType::TextureSampler);

    // Single top level acceleration structures
    ShaderBinding(uint32_t index, ShaderStage, const TopLevelAS*);

    // Multiple sampled textures in an array of fixed size (count)
    ShaderBinding(uint32_t index, ShaderStage, const std::vector<const Texture*>&, uint32_t count);

    // Multiple storage buffers in a dynamic array
    ShaderBinding(uint32_t index, ShaderStage, const std::vector<const Buffer*>&);

    uint32_t bindingIndex;
    uint32_t count;

    ShaderStage shaderStage;

    ShaderBindingType type;
    const TopLevelAS* tlas;
    std::vector<const Buffer*> buffers;
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
                Viewport viewport, BlendState blendState, RasterState rasterState, DepthState depthState)
        : m_renderTarget(renderTarget)
        , m_vertexLayout(vertexLayout)
        , m_shader(shader)
        , m_shaderBindingSets(shaderBindingSets)
        , m_viewport(viewport)
        , m_blendState(blendState)
        , m_rasterState(rasterState)
        , m_depthState(depthState)
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
    const DepthState& depthState() const { return m_depthState; }

private:
    const RenderTarget& m_renderTarget;
    VertexLayout m_vertexLayout;

    Shader m_shader;
    std::vector<const BindingSet*> m_shaderBindingSets;

    Viewport m_viewport;
    BlendState m_blendState;
    RasterState m_rasterState;
    DepthState m_depthState;
};

class RenderStateBuilder {
public:
    RenderStateBuilder(const RenderTarget&, const Shader&, const VertexLayout&);

    const RenderTarget& renderTarget;
    const VertexLayout& vertexLayout;
    const Shader& shader;

    bool writeDepth { true };
    bool testDepth { true };
    PolygonMode polygonMode { PolygonMode::Filled };

    [[nodiscard]] Viewport viewport() const;
    [[nodiscard]] BlendState blendState() const;
    [[nodiscard]] RasterState rasterState() const;
    [[nodiscard]] DepthState depthState() const;

    RenderStateBuilder& addBindingSet(const BindingSet&);
    [[nodiscard]] const std::vector<const BindingSet*>& bindingSets() const;

private:
    std::optional<Viewport> m_viewport {};
    std::optional<BlendState> m_blendState {};
    std::optional<RasterState> m_rasterState {};
    std::vector<const BindingSet*> m_bindingSets {};
};

struct RTTriangleGeometry {
    const Buffer& vertexBuffer;
    VertexFormat vertexFormat;
    size_t vertexStride;

    const Buffer& indexBuffer;
    IndexType indexType;

    mat4 transform;
};

struct RTAABBGeometry {
    const Buffer& aabbBuffer;
    size_t aabbStride;
};

class RTGeometry {
public:
    RTGeometry(RTTriangleGeometry);
    RTGeometry(RTAABBGeometry);

    bool hasTriangles() const;
    bool hasAABBs() const;

    const RTTriangleGeometry& triangles() const;
    const RTAABBGeometry& aabbs() const;

private:
    std::variant<RTTriangleGeometry, RTAABBGeometry> m_internal;
};

class BottomLevelAS : public Resource {
public:
    BottomLevelAS() = default;
    BottomLevelAS(const BottomLevelAS&) = default;
    BottomLevelAS(Badge<Registry>, std::vector<RTGeometry>);

    [[nodiscard]] const std::vector<RTGeometry>& geometries() const;

private:
    std::vector<RTGeometry> m_geometries {};
};

struct RTGeometryInstance {
    const BottomLevelAS& blas;
    const Transform& transform;
};

class TopLevelAS : public Resource {
public:
    TopLevelAS() = default;
    TopLevelAS(const TopLevelAS&) = default;
    TopLevelAS(Badge<Registry>, std::vector<RTGeometryInstance>);

    [[nodiscard]] const std::vector<RTGeometryInstance>& instances() const;
    [[nodiscard]] uint32_t instanceCount() const;

private:
    std::vector<RTGeometryInstance> m_instances {};
};

class HitGroup {
public:
    explicit HitGroup(ShaderFile closestHit, std::optional<ShaderFile> anyHit = {}, std::optional<ShaderFile> intersection = {});

    const ShaderFile& closestHit() const { return m_closestHit; }

    bool hasAnyHitShader() const { return m_anyHit.has_value(); }
    const ShaderFile& anyHit() const { return m_anyHit.value(); }

    bool hasIntersectionShader() const { return m_intersection.has_value(); }
    const ShaderFile& intersection() const { return m_intersection.value(); }

private:
    ShaderFile m_closestHit;
    std::optional<ShaderFile> m_anyHit;
    std::optional<ShaderFile> m_intersection;
};

class ShaderBindingTable {
public:
    // See https://www.willusher.io/graphics/2019/11/20/the-sbt-three-ways for all info you might want about SBT stuff!
    // TODO: Add support for ShaderRecord instead of just shader file, so we can include parameters to the records.

    ShaderBindingTable(ShaderFile rayGen, std::vector<HitGroup> hitGroups, std::vector<ShaderFile> missShaders);

    const ShaderFile& rayGen() const { return m_rayGen; }
    const std::vector<HitGroup>& hitGroups() const { return m_hitGroups; }
    const std::vector<ShaderFile>& missShaders() const { return m_missShaders; }

    std::vector<ShaderFile> allReferencedShaderFiles() const;

private:
    // TODO: In theory we can have more than one ray gen shader!
    ShaderFile m_rayGen;
    std::vector<HitGroup> m_hitGroups;
    std::vector<ShaderFile> m_missShaders;
};

class RayTracingState : public Resource {
public:
    RayTracingState() = default;
    RayTracingState(Badge<Registry>, ShaderBindingTable, std::vector<const BindingSet*>, uint32_t maxRecursionDepth);

    [[nodiscard]] uint32_t maxRecursionDepth() const;
    [[nodiscard]] const ShaderBindingTable& shaderBindingTable() const;
    [[nodiscard]] const std::vector<const BindingSet*>& bindingSets() const;

private:
    ShaderBindingTable m_shaderBindingTable;
    std::vector<const BindingSet*> m_bindingSets;
    uint32_t m_maxRecursionDepth;
};

class ComputeState : public Resource {
public:
    ComputeState() = default;
    ComputeState(Badge<Registry>, const Shader&, std::vector<const BindingSet*>);

    const Shader& shader() const { return m_shader; }
    [[nodiscard]] const std::vector<const BindingSet*>& bindingSets() const { return m_bindingSets; }

private:
    Shader m_shader;
    std::vector<const BindingSet*> m_bindingSets;
};
