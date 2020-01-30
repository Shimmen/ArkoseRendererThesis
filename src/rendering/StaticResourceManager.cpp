#include "StaticResourceManager.h"

#include "backend/Backend.h"

StaticResourceManager::StaticResourceManager()
    : m_resourceManager(nullptr)
    , m_staticAllocator(10 * 1024 * 1024)
{
    m_resourceManager.setCurrentNode("[static]");
}

StaticAllocator& StaticResourceManager::allocator()
{
    return m_staticAllocator;
}

Texture& StaticResourceManager::loadTexture(const std::string& imagePath, bool srgb, bool generateMipmaps)
{
    Texture& texture = m_resourceManager.loadTexture2D(imagePath, srgb, generateMipmaps);
    m_newTextures.push_back(&texture);
    return texture;
}
