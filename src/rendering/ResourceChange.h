#pragma once

#include "Resources.h"

class BufferUpdate {
public:
    BufferUpdate(Buffer buffer, std::vector<std::byte>&& data)
        : m_buffer(buffer)
        , m_data(data)
    {
    }

    const Buffer& buffer() const { return m_buffer; }
    const std::vector<std::byte>& data() const { return m_data; }

private:
    Buffer m_buffer;
    std::vector<std::byte> m_data;
};

class TextureUpdate {
public:
    explicit TextureUpdate(const Texture2D& texture, bool generateMipmaps)
        : m_texture(texture)
        , m_generateMipmaps(generateMipmaps)
    {
    }

    const Texture2D& texture() const { return m_texture; }
    bool generateMipmaps() const { return m_generateMipmaps; }

private:
    Texture2D m_texture;
    bool m_generateMipmaps;
};

class TextureUpdateFromFile : public TextureUpdate {
public:
    TextureUpdateFromFile(const Texture2D& texture, std::string path, bool generateMipmaps)
        : TextureUpdate(texture, generateMipmaps)
        , m_path(std::move(path))
    {
    }

    const std::string& path() const { return m_path; }

private:
    std::string m_path;
};

class TextureUpdateFromData : public TextureUpdate {
public:
    std::vector<const char> m_data;
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