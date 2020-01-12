#include "ResourceManager.h"

#include "utility/fileio.h"
#include "utility/logging.h"
#include "utility/util.h"
#include <stb_image.h>

ResourceManager::ResourceManager()
{
    m_buffers.reserve(maxNumBuffers);
    m_textures.reserve(maxNumTextures);
    m_renderTargets.reserve(maxNumRenderTargets);
    m_renderStates.reserve(maxNumRenderStates);
}

void ResourceManager::setCurrentPass(std::string pass)
{
    m_current_pass_name = std::move(pass);
}

RenderTarget& ResourceManager::getWindowRenderTarget()
{
    // NOTE: We don't want to add this to the list of render targets, since it's implied that it will always exist!
    static RenderTarget sharedWindowRenderTarget { Badge<ResourceManager>() };
    return sharedWindowRenderTarget;
}

RenderTarget& ResourceManager::createRenderTarget(std::initializer_list<RenderTarget::Attachment> attachments)
{
    if (m_renderTargets.size() >= m_renderTargets.capacity()) {
        LogErrorAndExit("Reached max capacity of render targets, update the capacity!\n");
    }
    RenderTarget renderTarget { {}, attachments };
    m_renderTargets.push_back(renderTarget);
    return m_renderTargets.back();
}

Texture2D& ResourceManager::createTexture2D(int width, int height, Texture2D::Format format)
{
    if (m_textures.size() >= m_textures.capacity()) {
        LogErrorAndExit("Reached max capacity of textures, update the capacity!\n");
    }
    Texture2D texture { {}, width, height, format, Texture2D::MinFilter::Linear, Texture2D::MagFilter::Linear };
    m_textures.push_back(texture);
    return m_textures.back();
}

Buffer& ResourceManager::createBuffer(size_t size, Buffer::Usage usage, Buffer::MemoryHint memoryHint)
{
    ASSERT(size > 0);
    if (m_buffers.size() >= m_buffers.capacity()) {
        LogErrorAndExit("Reached max capacity of buffers, update the capacity!\n");
    }
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

Texture2D& ResourceManager::loadTexture2D(std::string imagePath, bool srgb, bool generateMipmaps)
{
    if (!fileio::isFileReadable(imagePath)) {
        LogErrorAndExit("Could not read image at path '%s'.\n", imagePath.c_str());
    }

    int width, height, componentCount;
    stbi_info(imagePath.c_str(), &width, &height, &componentCount);

    // TODO!
    ASSERT(componentCount == 3 || componentCount == 4);
    auto format = Texture2D::Format::RGBA8;

    Texture2D& texture = createTexture2D(width, height, format);
    m_immediate_texture_updates.emplace_back(texture, imagePath, generateMipmaps);

    return texture;
}

RenderState& ResourceManager::createRenderState(
    const RenderTarget& renderTarget, const VertexLayout& vertexLayout,
    const Shader& shader, const ShaderBindingSet& shaderBindingSet,
    const Viewport& viewport, const BlendState& blendState)
{
    if (m_renderStates.size() >= m_renderStates.capacity()) {
        LogErrorAndExit("Reached max capacity of render states, update the capacity!\n");
    }
    RenderState renderState = { {}, renderTarget, vertexLayout, shader, shaderBindingSet, viewport, blendState };
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

void ResourceManager::publish(const std::string& name, const Texture2D& texture)
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

const Texture2D* ResourceManager::getTexture2D(const std::string& renderPass, const std::string& name)
{
    std::string fullName = makeQualifiedName(renderPass, name);
    auto entry = m_name_texture_map.find(fullName);

    if (entry == m_name_texture_map.end()) {
        return nullptr;
    }

    const Texture2D* texture = entry->second;
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

const std::vector<RenderState>& ResourceManager::renderStates() const
{
    return m_renderStates;
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
