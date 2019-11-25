#pragma once

#include "utility/copying.h"
#include "utility/mathkit.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

constexpr uint32_t NullHandle = 0u;

struct Extent2D {
    Extent2D(int width, int height)
        : m_width(width)
        , m_height(height)
    {
    }

    [[nodiscard]] int width() const { return m_width; }
    [[nodiscard]] int height() const { return m_height; }

    bool operator!=(const Extent2D& other) const
    {
        return !(*this == other);
    }
    bool operator==(const Extent2D& other) const
    {
        return m_width == other.m_width && m_height == other.m_height;
    }

    int m_width {};
    int m_height {};
};

struct Texture2D {
    //NON_COPYABLE(Texture2D)

    enum class Components {
        Grayscale,
        Rgb,
        Rgba,
    };

    Texture2D() = default;
    Texture2D(int width, int height, Components, bool srgb = true, bool mipmaps = true);
    //Texture2D(Texture2D&&) noexcept;
    ~Texture2D();

    Extent2D extent { 0, 0 };
    Components components { Components::Grayscale };
    bool mipmaps { false };
    bool srgb { false };

private:
    uint32_t m_handle { NullHandle };
};

struct RenderTarget {
    NON_COPYABLE(RenderTarget)

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

    explicit RenderTarget(Texture2D&);
    RenderTarget(std::initializer_list<Attachment>);
    RenderTarget(RenderTarget&&) noexcept;

    [[nodiscard]] Extent2D extent() const;
    [[nodiscard]] size_t attachmentCount() const;
    [[nodiscard]] bool hasDepthTarget() const;

private:
    std::vector<Attachment> m_attachments {};
    std::optional<Attachment> m_depthAttachment {};
};

struct Buffer {
    //NON_COPYABLE(Buffer)

    enum class Usage {
        TransferOptimal,
        GpuOptimal,
    };

    template<typename T>
    static inline Buffer createStatic(const std::vector<T>& data)
    {
        size_t size = data.size() * sizeof(T);
        Buffer buffer { size, Usage::GpuOptimal };
        buffer.setData(data.data(), size, 0);
        return buffer;
    }

    Buffer() = default;
    Buffer(size_t size, Usage);
    //Buffer(Buffer&&) noexcept;
    ~Buffer();

    void setData(void* data, size_t size, size_t offset = 0);

    size_t size { 0 };
    Usage usage { Usage::GpuOptimal };

private:
    uint32_t m_handle {};
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

    Shader(std::string name, std::vector<ShaderFile>, ShaderType type);
    ~Shader();

    [[nodiscard]] ShaderType type() const;

    // TODO: We should maybe add some utility API for shader introspection here..?
    //  Somehow we need to extract descriptor sets etc.
    //  but maybe that is backend-specific or file specific?

private:
    std::string m_name;
    std::vector<ShaderFile> m_files;
    ShaderType m_type;
    uint32_t m_handle {};
};
