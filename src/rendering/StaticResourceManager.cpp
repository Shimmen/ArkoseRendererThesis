#include "StaticResourceManager.h"

#include "backend/Backend.h"

StaticResourceManager::StaticResourceManager()
    : m_resourceManager(nullptr)
{
    m_resourceManager.setCurrentPass("static");
}

Texture& StaticResourceManager::loadTexture2D(std::string imagePath, bool srgb, bool generateMipmaps)
{
    Texture& texture = m_resourceManager.loadTexture2D(std::move(imagePath), srgb, generateMipmaps);
    return texture;
}
