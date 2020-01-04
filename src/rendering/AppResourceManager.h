#pragma once

#include "ResourceManager.h"
#include <vector>

class Backend;

class AppResourceManager {

public:
    explicit AppResourceManager(int multiplicity);

    // TODO: Add some type safe StaticBuffer/StaticTexture2D object for this type of stuff maybe?

    template<typename T>
    [[nodiscard]] Buffer createStaticBuffer(Buffer::Usage, std::vector<T>&&);
    [[nodiscard]] Buffer createDynamicBuffer(size_t size, Buffer::Usage);

    [[nodiscard]] Texture2D loadStaticTexture2D(std::string imagePath, bool srgb, bool generateMipmaps);

private:
    int m_multiplicity;
    std::vector<ResourceManager> m_resourceManagers {};
};

template<typename T>
Buffer AppResourceManager::createStaticBuffer(Buffer::Usage usage, std::vector<T>&& data)
{
    Buffer buffer = m_resourceManagers.front().createBuffer<T>(std::move(data), usage);
    return buffer;
}
