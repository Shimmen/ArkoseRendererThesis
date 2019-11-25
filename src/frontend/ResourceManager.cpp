#include "ResourceManager.h"

#include "backend/Backend.h"

ResourceManager::ResourceManager(ApplicationState appState, Backend& backend)
    : m_appState(std::move(appState))
    , m_backend(backend)
{
}

RenderTarget ResourceManager::getWindowRenderTarget()
{
    return {};
}

RenderTarget ResourceManager::createRenderTarget(std::initializer_list<RenderTarget::Attachment>)
{
    return {};
}

Texture2D ResourceManager::createTexture2D(int width, int height, Texture2D::Components components, bool mipmaps, bool srgb)
{
    return {};
}

Texture2D ResourceManager::getTexture2D(std::string renderPass, std::string name)
{
    return {};
}

Buffer ResourceManager::createBuffer(size_t size, Buffer::Usage usage)
{
    return {};
}

Buffer ResourceManager::createBuffer(const void* data, size_t size, Buffer::Usage usage)
{
    Buffer buffer = createBuffer(size, usage);
    setBufferDataImmediately(buffer, data, size);
    return buffer;
}

Texture2D ResourceManager::loadTexture2D(std::string imagePath, bool generateMipmaps)
{
    return {};
}

void ResourceManager::setBufferDataImmediately(const Buffer&, const void* data, size_t size, size_t offset)
{
}
