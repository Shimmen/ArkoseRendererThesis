#include "AppResourceManager.h"

#include "backend/Backend.h"

AppResourceManager::AppResourceManager(int multiplicity)
    : m_multiplicity(multiplicity)
{
    m_resourceManagers.reserve(multiplicity);
    for (size_t i = 0; i < multiplicity; ++i) {
        m_resourceManagers.emplace_back(i);
    }
}

Buffer AppResourceManager::createDynamicBuffer(size_t size, Buffer::Usage usage)
{
    for (size_t i = 0; i < m_multiplicity; ++i) {
        Buffer buffer = m_resourceManagers[i].createBuffer(size, usage);
    }

    // TODO: Yeah, what are we supposed to return here exactly..? Some kind of container for all of them? I dunno..
    return {};
}

Texture2D AppResourceManager::loadStaticTexture2D(std::string imagePath, bool srgb, bool generateMipmaps)
{
    return m_resourceManagers.front().loadTexture2D(std::move(imagePath), srgb, generateMipmaps);
}
