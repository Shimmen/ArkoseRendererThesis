#include "ResourceManager.h"

#include "utility/fileio.h"
#include "utility/logging.h"
#include "utility/util.h"
#include <stb_image.h>

ResourceManager::ResourceManager(const RenderTarget* windowRenderTarget)
    : m_windowRenderTarget(windowRenderTarget)
{
}

void ResourceManager::setCurrentNode(std::string node)
{
    m_currentNodeName = std::move(node);
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
    Texture texture { {}, extent, format, usage, Texture::MinFilter::Linear, Texture::MagFilter::Linear, Texture::Mipmap::None };
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

Texture& ResourceManager::loadTexture2D(const std::string& imagePath, bool srgb, bool generateMipmaps)
{
    if (!fileio::isFileReadable(imagePath)) {
        LogErrorAndExit("Could not read image at path '%s'.\n", imagePath.c_str());
    }

    int width, height, componentCount;
    stbi_info(imagePath.c_str(), &width, &height, &componentCount);

    Texture::Format format;
    switch (componentCount) {
    case 3:
        // NOTE: sRGB (without alpha) is often not supported, so we don't support it in ArkoseRenderer
        format = (srgb) ? Texture::Format::sRGBA8 : Texture::Format::RGB8;
        break;
    case 4:
        format = (srgb) ? Texture::Format::sRGBA8 : Texture::Format::RGBA8;
        break;
    default:
        LogErrorAndExit("Currently no support for other than (s)RGB and (s)RGBA texture loading!\n");
    }

    // TODO: Maybe we want to allow more stuff..?
    auto usage = Texture::Usage::Sampled;

    auto mipmapMode = generateMipmaps ? Texture::Mipmap::Linear : Texture::Mipmap::None;

    m_textures.push_back({ {}, { width, height }, format, usage, Texture::MinFilter::Linear, Texture::MagFilter::Linear, mipmapMode });
    Texture& texture = m_textures.back();

    m_immediateTextureUpdates.emplace_back(texture, imagePath, generateMipmaps);

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
    ASSERT(m_currentNodeName.has_value());
    std::string fullName = makeQualifiedName(m_currentNodeName.value(), name);
    auto entry = m_nameBufferMap.find(fullName);
    ASSERT(entry == m_nameBufferMap.end());
    m_nameBufferMap[fullName] = &buffer;
}

void ResourceManager::publish(const std::string& name, const Texture& texture)
{
    ASSERT(m_currentNodeName.has_value());
    std::string fullName = makeQualifiedName(m_currentNodeName.value(), name);
    auto entry = m_nameTextureMap.find(fullName);
    ASSERT(entry == m_nameTextureMap.end());
    m_nameTextureMap[fullName] = &texture;
}

void ResourceManager::publish(const std::string& name, const RenderTarget& renderTarget)
{
    ASSERT(m_currentNodeName.has_value());
    std::string fullName = makeQualifiedName(m_currentNodeName.value(), name);
    auto entry = m_nameRenderTargetMap.find(fullName);
    ASSERT(entry == m_nameRenderTargetMap.end());
    m_nameRenderTargetMap[fullName] = &renderTarget;
}

const Texture* ResourceManager::getTexture2D(const std::string& renderPass, const std::string& name)
{
    ASSERT(m_currentNodeName.has_value());

    std::string fullName = makeQualifiedName(renderPass, name);
    auto entry = m_nameTextureMap.find(fullName);

    if (entry == m_nameTextureMap.end()) {
        return nullptr;
    }

    NodeDependency dependency { m_currentNodeName.value(), renderPass };
    m_nodeDependencies.insert(dependency);

    const Texture* texture = entry->second;
    return texture;
}

const Buffer* ResourceManager::getBuffer(const std::string& renderPass, const std::string& name)
{
    ASSERT(m_currentNodeName.has_value());

    std::string fullName = makeQualifiedName(renderPass, name);
    auto entry = m_nameBufferMap.find(fullName);

    if (entry == m_nameBufferMap.end()) {
        return nullptr;
    }

    NodeDependency dependency { m_currentNodeName.value(), renderPass };
    m_nodeDependencies.insert(dependency);

    const Buffer* buffer = entry->second;
    return buffer;
}

const std::unordered_set<NodeDependency>& ResourceManager::nodeDependencies() const
{
    return m_nodeDependencies;
}

void ResourceManager::setBufferDataImmediately(Buffer& buffer, const std::byte* data, size_t size)
{
    // TODO: Don't make a copy here! I think it should be made explicit at the calling site.
    std::vector<std::byte> data_copy { data, data + size };
    m_immediateBufferUpdates.emplace_back(buffer, std::move(data_copy));
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
    return m_immediateBufferUpdates;
}

const std::vector<TextureUpdateFromFile>& ResourceManager::textureUpdates() const
{
    return m_immediateTextureUpdates;
}

Badge<ResourceManager> ResourceManager::exchangeBadges(Badge<Backend>) const
{
    return {};
}

std::string ResourceManager::makeQualifiedName(const std::string& node, const std::string& name)
{
    return node + ':' + name;
}
