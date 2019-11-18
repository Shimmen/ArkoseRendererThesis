#pragma once

#include "types.h"
#include "utility/copying.h"
#include "utility/mathkit.h"
#include <optional>
#include <string>
#include <vector>

constexpr uint32_t NullHandle = 0u;

struct Extent2D {
    Extent2D(int width, int height)
        : width(width)
        , height(height)
    {
    }

    bool operator!=(const Extent2D& other) const
    {
        return !(*this == other);
    }
    bool operator==(const Extent2D& other) const
    {
        return width == other.width && height == other.height;
    }

    const int width {};
    const int height {};
};

struct Texture2D {
    NON_COPYABLE(Texture2D)

    enum class Components {
        Grayscale,
        Rgb,
        Rgba,
    };

    Texture2D(int width, int height, Components, bool srgb = true, bool mipmaps = true);
    Texture2D(Texture2D&&) noexcept;
    ~Texture2D();

    const Extent2D extent;
    const Components components;
    const bool mipmaps;
    const bool srgb;

private:
    uint32_t m_handle { NullHandle };
};

struct Framebuffer {
    NON_COPYABLE(Framebuffer)

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

    explicit Framebuffer(Texture2D&);
    Framebuffer(std::initializer_list<Attachment>);
    Framebuffer(Framebuffer&&) noexcept;

    [[nodiscard]] Extent2D extent() const;
    [[nodiscard]] size_t attachmentCount() const;
    [[nodiscard]] bool hasDepthTarget() const;

private:
    std::vector<Attachment> m_attachments {};
    std::optional<Attachment> m_depthAttachment {};
};

struct Buffer {
    NON_COPYABLE(Buffer)

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

    Buffer(size_t size, Usage);
    Buffer(Buffer&&) noexcept;
    ~Buffer();

    void setData(void* data, size_t size, size_t offset = 0);

    const size_t size;
    const Usage usage;

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
