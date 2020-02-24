#pragma once

#include "Resources.h"
#include <variant>

class BufferUpdate {
public:
    BufferUpdate(Buffer& buffer, std::vector<std::byte>&& data)
        : m_buffer(buffer)
        , m_data(data)
    {
    }

    const Buffer& buffer() const { return m_buffer; }
    const std::vector<std::byte>& data() const { return m_data; }

private:
    Buffer& m_buffer;
    std::vector<std::byte> m_data;
};

class TextureUpdate {
public:
    TextureUpdate(Texture& texture, std::string path, bool generateMipmaps)
        : m_texture(texture)
        , m_generateMipmaps(generateMipmaps)
        , m_value(std::move(path))
    {
    }

    TextureUpdate(Texture& texture, vec4 pixelValue)
        : m_texture(texture)
        , m_generateMipmaps(false)
        , m_value(pixelValue)
    {
    }

    const Texture& texture() const { return m_texture; }
    bool generateMipmaps() const { return m_generateMipmaps; }

    bool hasPath() const { return std::get_if<std::string>(&m_value) != nullptr; }
    bool hasPixelValue() const { return std::get_if<vec4>(&m_value) != nullptr; }

    std::string path() const { return *std::get_if<std::string>(&m_value); }
    vec4 pixelValue() const { return *std::get_if<vec4>(&m_value); }

private:
    Texture& m_texture;
    bool m_generateMipmaps;
    std::variant<std::string, vec4> m_value;
};

class ResourceActions {
public:
private:
    std::vector<Buffer> m_buffers_to_create {};
    std::vector<Buffer> m_buffers_to_delete {};
};

class ResourceChange {
public:
    enum class Type {
        Buffer,
        Texture,
        ImmediateBufferUpdate
    };

private:
};
