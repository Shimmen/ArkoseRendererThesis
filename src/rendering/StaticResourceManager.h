#pragma once

#include "ResourceManager.h"
#include "utility/ArenaAllocator.h"
#include "utility/Badge.h"
#include <vector>

using StaticAllocator = ArenaAllocator<class StaticResourceManager>;

class StaticResourceManager {
public:
    StaticResourceManager();

    StaticAllocator& allocator();

    template<typename T>
    [[nodiscard]] Buffer& createBuffer(Buffer::Usage, std::vector<T>&&);

    [[nodiscard]] Texture& loadTexture(const std::string& imagePath, bool srgb, bool generateMipmaps);

    ResourceManager& internal(Badge<Backend>)
    {
        return m_resourceManager;
    }

private:
    ResourceManager m_resourceManager;
    StaticAllocator m_staticAllocator;

    // TODO: Should we allow nodes to add stuff to the static manager after startup?
    std::vector<Buffer*> m_newBuffers {};
    std::vector<Texture*> m_newTextures {};
};

template<typename T>
Buffer& StaticResourceManager::createBuffer(Buffer::Usage usage, std::vector<T>&& data)
{
    Buffer& buffer = m_resourceManager.createBuffer<T>(std::move(data), usage, Buffer::MemoryHint::GpuOptimal);
    m_newBuffers.push_back(&buffer);
    return buffer;
}
