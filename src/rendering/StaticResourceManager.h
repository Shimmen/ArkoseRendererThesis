#pragma once

#include "ResourceManager.h"
#include "utility/Badge.h"
#include <vector>

class StaticResourceManager {

public:
    explicit StaticResourceManager();

    // TODO: Add some type safe StaticBuffer/StaticTexture2D object for this type of stuff maybe?
    //  On the other hand, who in 'userland' could take advantage of a Buffer object? No one, I think
    //  because only a backend can do stuff with it, and it knows what to do.

    template<typename T>
    [[nodiscard]] Buffer& createBuffer(Buffer::Usage, std::vector<T>&&);

    [[nodiscard]] Texture& loadTexture2D(std::string imagePath, bool srgb, bool generateMipmaps);

    ResourceManager& internal(Badge<Backend>) { return m_resourceManager; }

private:
    ResourceManager m_resourceManager;
};

template<typename T>
Buffer& StaticResourceManager::createBuffer(Buffer::Usage usage, std::vector<T>&& data)
{
    Buffer& buffer = m_resourceManager.createBuffer<T>(std::move(data), usage, Buffer::MemoryHint::GpuOptimal);
    return buffer;
}
