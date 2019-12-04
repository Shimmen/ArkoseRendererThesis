#include "ResourceManager.h"

#include "utility/util.h"

ResourceManager::ResourceManager(Backend& backend)
    : m_backend(backend)
{
}

RenderTarget ResourceManager::getWindowRenderTarget()
{
    // TODO: Implement properly!
    return RenderTarget({}, createTexture2D(1024, 1024, Texture2D::Components::Rgb, true, false));
}

RenderTarget ResourceManager::createRenderTarget(std::initializer_list<RenderTarget::Attachment>)
{
    // TODO: Implement properly!
    return RenderTarget({}, createTexture2D(512, 512, Texture2D::Components::Rgb, true, false));
}

Texture2D ResourceManager::createTexture2D(int width, int height, Texture2D::Components components, bool srgb, bool mipmaps)
{
    return Texture2D({}, width, height, components, srgb, mipmaps);
}

Texture2D ResourceManager::getTexture2D(std::string renderPass, std::string name)
{
    // TODO: Implement properly! (get resource from global cache)
    return Texture2D({}, 64, 64, Texture2D::Components::Grayscale, false, false);
}

Buffer ResourceManager::createBuffer(size_t size, Buffer::Usage usage)
{
    ASSERT(size > 0);
    return Buffer({}, size, usage);
}

Buffer ResourceManager::createBuffer(const void* data, size_t size, Buffer::Usage usage)
{
    Buffer buffer = createBuffer(size, usage);
    setBufferDataImmediately(buffer, data, size);
    return buffer;
}

Texture2D ResourceManager::loadTexture2D(std::string imagePath, bool generateMipmaps)
{
    // TODO: Implement properly
    return createTexture2D(128, 128, Texture2D::Components::Rgba, true, false);
}

void ResourceManager::setBufferDataImmediately(const Buffer&, const void* data, size_t size, size_t offset)
{
    // TODO: How exactly?
}
