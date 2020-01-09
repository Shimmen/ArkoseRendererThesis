#include "StaticResourceManager.h"

#include "backend/Backend.h"

StaticResourceManager::StaticResourceManager()
    : m_resourceManager()
{
    m_resourceManager.setCurrentPass("static");
}

Texture2D& StaticResourceManager::loadTexture2D(std::string imagePath, bool srgb, bool generateMipmaps)
{
    Texture2D& texture = m_resourceManager.loadTexture2D(std::move(imagePath), srgb, generateMipmaps);
    return texture;
}
