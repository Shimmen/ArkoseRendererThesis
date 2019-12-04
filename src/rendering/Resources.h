#pragma once

#include "utility/Badge.h"
#include "utility/copying.h"
#include "utility/mathkit.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

constexpr uint32_t NullHandle = 0u;

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
    // TODO: We probably want to require a Badge for access to these
    [[nodiscard]] uint64_t id() const;
    void registerBackend(Badge<Backend>, uint64_t id);

private:
    uint64_t m_id { UINT64_MAX };
};

struct Texture2D {

    enum class Components {
        Grayscale,
        Rgb,
        Rgba,
    };

    Texture2D(Badge<ResourceManager>, int width, int height, Components, bool srgb = true, bool mipmaps = true);

    [[nodiscard]] const Extent2D& extent() const { return m_extent; }
    [[nodiscard]] Components components() const { return m_components; }
    [[nodiscard]] bool hasMipmaps() const { return m_mipmaps; }
    [[nodiscard]] bool isSrgb() const { return m_srgb; }

private:
    Extent2D m_extent;
    Components m_components;
    bool m_mipmaps;
    bool m_srgb;
};

struct RenderTarget : public Resource {

    enum class AttachmentType {
        Color0,
        Color1,
        Color2,
        Color3,
        Depth
    };

    struct Attachment {
        AttachmentType type;
        Texture2D* texture;
    };

    RenderTarget() = default;
    explicit RenderTarget(Badge<ResourceManager>, Texture2D&&);
    explicit RenderTarget(Badge<ResourceManager>, std::initializer_list<Attachment> targets);

    [[nodiscard]] const Extent2D& extent() const;
    [[nodiscard]] size_t attachmentCount() const;
    [[nodiscard]] bool hasDepthAttachment() const;
    [[nodiscard]] bool isWindowTarget() const;

private:
    std::vector<Attachment> m_attachments {};
    std::optional<Attachment> m_depthAttachment {};
    bool m_isWindowTarget { false };
};

struct Buffer : public Resource {

    enum class Usage {
        TransferOptimal,
        GpuOptimal,
    };

    Buffer() = default;
    Buffer(Badge<ResourceManager>, size_t size, Usage);

private:
    size_t m_size { 0 };
    Usage m_usage { Usage::GpuOptimal };
};

enum class ShaderFileType {
    Vertex,
    Fragment,
    Compute,
};

struct ShaderFile {
    ShaderFile(std::string name, ShaderFileType type);

    [[nodiscard]] ShaderFileType type() const;

private:
    std::string m_name;
    ShaderFileType m_type;
};

enum class ShaderType {
    Raster,
    Compute
};

struct Shader {

    static Shader createBasic(std::string name, std::string vertexName, std::string fragmentName);
    static Shader createCompute(std::string name, std::string computeName);

    Shader() = default;
    Shader(std::string name, std::vector<ShaderFile>, ShaderType type);
    ~Shader();

    [[nodiscard]] ShaderType type() const;

    // TODO: We should maybe add some utility API for shader introspection here..?
    //  Somehow we need to extract descriptor sets etc.
    //  but maybe that is backend-specific or file specific?

private:
    std::string m_name {};
    std::vector<ShaderFile> m_files {};
    ShaderType m_type {};
};
