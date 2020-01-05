#include "ResourceManager.h"

#include "utility/fileio.h"
#include "utility/logging.h"
#include "utility/util.h"
#include <stb_image.h>

ResourceManager::ResourceManager(int frameAssociation)
    : m_frameAssociation(frameAssociation)
{
}

void ResourceManager::setCurrentPass(std::string pass)
{
    m_current_pass_name = std::move(pass);
}

RenderTarget ResourceManager::getWindowRenderTarget()
{
    // TODO: Should we set up a stack of attachments represented on the inside of the backend, or should
    //  this just be like a fake front-end for the Textures etc it represresents? ey?
    //m_backend.

    // TODO: Implement properly!
    Texture2D texture = createTexture2D(1024, 1024, Texture2D::Format::RGBA8);
    return RenderTarget({}, texture);
}

RenderTarget ResourceManager::createRenderTarget(std::initializer_list<RenderTarget::Attachment> attachments)
{
    RenderTarget renderTarget { {}, attachments };
    m_renderTargets.push_back(renderTarget);
    return renderTarget;
}

Texture2D ResourceManager::createTexture2D(int width, int height, Texture2D::Format format)
{
    Texture2D texture { {}, width, height, format, Texture2D::MinFilter::Linear, Texture2D::MagFilter::Linear };
    m_textures.push_back(texture);
    return texture;
}

std::optional<Texture2D> ResourceManager::getTexture2D(const std::string& renderPass, const std::string& name)
{
    std::string fullName = makeQualifiedName(renderPass, name);
    auto entry = m_name_texture_map.find(fullName);

    if (entry == m_name_texture_map.end()) {
        return {};
    }

    const Texture2D& texture = entry->second;
    return texture;
}

Buffer ResourceManager::createBuffer(size_t size, Buffer::Usage usage)
{
    ASSERT(size > 0);
    Buffer buffer = { {}, size, usage };
    m_buffers.push_back(buffer);
    return buffer;
}

Buffer ResourceManager::createBuffer(const std::byte* data, size_t size, Buffer::Usage usage)
{
    Buffer buffer = createBuffer(size, usage);
    setBufferDataImmediately(buffer, data, size);
    return buffer;
}

Texture2D ResourceManager::loadTexture2D(std::string imagePath, bool srgb, bool generateMipmaps)
{
    if (!fileio::isFileReadable(imagePath)) {
        LogErrorAndExit("Could not read image at path '%s'.\n", imagePath.c_str());
    }

    int width, height, componentCount;
    stbi_info(imagePath.c_str(), &width, &height, &componentCount);

    // TODO!
    ASSERT(componentCount == 3 || componentCount == 4);
    auto format = Texture2D::Format::RGBA8;

    Texture2D texture = createTexture2D(width, height, format);

    m_textures.push_back(texture);
    m_immediate_texture_updates.emplace_back(texture, imagePath, generateMipmaps);

    return texture;
}

void ResourceManager::assignName(const std::string& name, const Buffer& buffer)
{
    ASSERT(m_current_pass_name.has_value());
    std::string fullName = makeQualifiedName(m_current_pass_name.value(), name);
    auto entry = m_name_buffer_map.find(fullName);
    ASSERT(entry == m_name_buffer_map.end());
    m_name_buffer_map[fullName] = buffer;
}

void ResourceManager::assignName(const std::string& name, const Texture2D& texture)
{
    ASSERT(m_current_pass_name.has_value());
    std::string fullName = makeQualifiedName(m_current_pass_name.value(), name);
    auto entry = m_name_texture_map.find(fullName);
    ASSERT(entry == m_name_texture_map.end());
    m_name_texture_map[fullName] = texture;
}

void ResourceManager::assignName(const std::string& name, const RenderTarget& renderTarget)
{
    ASSERT(m_current_pass_name.has_value());
    std::string fullName = makeQualifiedName(m_current_pass_name.value(), name);
    auto entry = m_name_render_target_map.find(fullName);
    ASSERT(entry == m_name_render_target_map.end());
    m_name_render_target_map[fullName] = renderTarget;
}

void ResourceManager::setBufferDataImmediately(const Buffer& buffer, const std::byte* data, size_t size, size_t offset)
{
    // TODO: Don't make a copy here! I think it should be made explicit at the calling site.
    std::vector<std::byte> data_copy { data, data + size };
    m_immediate_buffer_updates.emplace_back(buffer, std::move(data_copy));
}

const std::vector<Buffer>& ResourceManager::buffers() const
{
    return m_buffers;
}

const std::vector<Texture2D>& ResourceManager::textures() const
{
    return m_textures;
}

const std::vector<RenderTarget>& ResourceManager::renderTargets() const
{
    return m_renderTargets;
}

const std::vector<BufferUpdate>& ResourceManager::bufferUpdates() const
{
    return m_immediate_buffer_updates;
}

const std::vector<TextureUpdateFromFile>& ResourceManager::textureUpdates() const
{
    return m_immediate_texture_updates;
}

std::string ResourceManager::makeQualifiedName(const std::string& pass, const std::string& name)
{
    return pass + ':' + name;
}
