#include "ResourceManager.h"

#include "utility/fileio.h"
#include "utility/logging.h"
#include "utility/util.h"
#include <stb_image.h>

ResourceManager::ResourceManager(const RenderTarget* windowRenderTarget)
    : m_windowRenderTarget(windowRenderTarget)
{
}

void ResourceManager::setCurrentPass(std::string pass)
{
    m_current_pass_name = std::move(pass);
}

const RenderTarget& ResourceManager::windowRenderTarget()
{
    ASSERT(m_windowRenderTarget);
    return *m_windowRenderTarget;
}

RenderTarget& ResourceManager::createRenderTarget(std::initializer_list<RenderTarget::Attachment> attachments)
{
    RenderTarget renderTarget { {}, attachments };
    m_renderTargets.push_back(renderTarget);
    return m_renderTargets.back();
}

Texture& ResourceManager::createTexture2D(Extent2D extent, Texture::Format format, Texture::Usage usage)
{
    Texture texture { {}, extent, format, usage, Texture::MinFilter::Linear, Texture::MagFilter::Linear };
    m_textures.push_back(texture);
    return m_textures.back();
}

Buffer& ResourceManager::createBuffer(size_t size, Buffer::Usage usage, Buffer::MemoryHint memoryHint)
{
    ASSERT(size > 0);
    Buffer buffer = { {}, size, usage, memoryHint };
    m_buffers.push_back(buffer);
    return m_buffers.back();
}

Buffer& ResourceManager::createBuffer(const std::byte* data, size_t size, Buffer::Usage usage, Buffer::MemoryHint memoryHint)
{
    Buffer& buffer = createBuffer(size, usage, memoryHint);
    setBufferDataImmediately(buffer, data, size);
    return buffer;
}

Texture& ResourceManager::loadTexture2D(std::string imagePath, bool srgb, bool generateMipmaps)
{
    if (!fileio::isFileReadable(imagePath)) {
        LogErrorAndExit("Could not read image at path '%s'.\n", imagePath.c_str());
    }

    int width, height, componentCount;
    stbi_info(imagePath.c_str(), &width, &height, &componentCount);

    Texture::Format format;
    switch (componentCount) {
    case 1:
    case 2:
        LogErrorAndExit("Currently no support for other than RGB and RGBA texture loading!\n");
    case 3:
        format = (srgb) ? Texture::Format::sRGB8 : Texture::Format::RGB8;
        break;
    case 4:
        format = (srgb) ? Texture::Format::sRGBA8 : Texture::Format::RGBA8;
        break;
    }

    // TODO: Maybe we want to allow more stuff..?
    auto usage = Texture::Usage::Sampled;

    Texture& texture = createTexture2D({ width, height }, format, usage);
    m_immediate_texture_updates.emplace_back(texture, imagePath, generateMipmaps);

    return texture;
}

RenderState& ResourceManager::createRenderState(
    const RenderTarget& renderTarget, const VertexLayout& vertexLayout,
    const Shader& shader, const ShaderBindingSet& shaderBindingSet,
    const Viewport& viewport, const BlendState& blendState, const RasterState& rasterState)
{
    RenderState renderState = { {}, renderTarget, vertexLayout, shader, shaderBindingSet, viewport, blendState, rasterState };
    m_renderStates.push_back(renderState);
    return m_renderStates.back();
}

void ResourceManager::publish(const std::string& name, const Buffer& buffer)
{
    ASSERT(m_current_pass_name.has_value());
    std::string fullName = makeQualifiedName(m_current_pass_name.value(), name);
    auto entry = m_name_buffer_map.find(fullName);
    ASSERT(entry == m_name_buffer_map.end());
    m_name_buffer_map[fullName] = &buffer;
}

void ResourceManager::publish(const std::string& name, const Texture& texture)
{
    ASSERT(m_current_pass_name.has_value());
    std::string fullName = makeQualifiedName(m_current_pass_name.value(), name);
    auto entry = m_name_texture_map.find(fullName);
    ASSERT(entry == m_name_texture_map.end());
    m_name_texture_map[fullName] = &texture;
}

void ResourceManager::publish(const std::string& name, const RenderTarget& renderTarget)
{
    ASSERT(m_current_pass_name.has_value());
    std::string fullName = makeQualifiedName(m_current_pass_name.value(), name);
    auto entry = m_name_render_target_map.find(fullName);
    ASSERT(entry == m_name_render_target_map.end());
    m_name_render_target_map[fullName] = &renderTarget;
}

const Texture* ResourceManager::getTexture2D(const std::string& renderPass, const std::string& name)
{
    std::string fullName = makeQualifiedName(renderPass, name);
    auto entry = m_name_texture_map.find(fullName);

    if (entry == m_name_texture_map.end()) {
        return nullptr;
    }

    const Texture* texture = entry->second;
    return texture;
}

void ResourceManager::setBufferDataImmediately(Buffer& buffer, const std::byte* data, size_t size, size_t offset)
{
    // TODO: Don't make a copy here! I think it should be made explicit at the calling site.
    std::vector<std::byte> data_copy { data, data + size };
    m_immediate_buffer_updates.emplace_back(buffer, std::move(data_copy));
}

const std::vector<Buffer>& ResourceManager::buffers() const
{
    return m_buffers.vector();
}

const std::vector<Texture>& ResourceManager::textures() const
{
    return m_textures.vector();
}

const std::vector<RenderTarget>& ResourceManager::renderTargets() const
{
    return m_renderTargets.vector();
}

const std::vector<RenderState>& ResourceManager::renderStates() const
{
    return m_renderStates.vector();
}

const std::vector<BufferUpdate>& ResourceManager::bufferUpdates() const
{
    return m_immediate_buffer_updates;
}

const std::vector<TextureUpdateFromFile>& ResourceManager::textureUpdates() const
{
    return m_immediate_texture_updates;
}

Badge<ResourceManager> ResourceManager::exchangeBadges(Badge<Backend>) const
{
    return {};
}

std::string ResourceManager::makeQualifiedName(const std::string& pass, const std::string& name)
{
    return pass + ':' + name;
}
