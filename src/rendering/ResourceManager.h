#pragma once

#include "ApplicationState.h"
#include "ResourceChange.h"
#include "Resources.h"
#include "utility/CapList.h"
#include "utility/util.h"
#include <unordered_map>

class ResourceManager {
public:
    explicit ResourceManager(const RenderTarget* windowRenderTarget);

    void setCurrentPass(std::string);

    [[nodiscard]] const RenderTarget& windowRenderTarget();
    [[nodiscard]] RenderTarget& createRenderTarget(std::initializer_list<RenderTarget::Attachment>);

    [[nodiscard]] Texture& loadTexture2D(std::string imagePath, bool srgb, bool generateMipmaps);
    [[nodiscard]] Texture& createTexture2D(Extent2D, Texture::Format);

    [[nodiscard]] Buffer& createBuffer(size_t size, Buffer::Usage, Buffer::MemoryHint);
    template<typename T>
    [[nodiscard]] Buffer& createBuffer(std::vector<T>&& inData, Buffer::Usage usage, Buffer::MemoryHint);
    [[nodiscard]] Buffer& createBuffer(const std::byte* data, size_t size, Buffer::Usage, Buffer::MemoryHint);

    [[nodiscard]] RenderState& createRenderState(const RenderTarget&, const VertexLayout&, const Shader&, const ShaderBindingSet&, const Viewport&, const BlendState&, const RasterState&);

    void publish(const std::string& name, const Buffer&);
    void publish(const std::string& name, const Texture&);
    void publish(const std::string& name, const RenderTarget&);

    [[nodiscard]] const Texture* getTexture2D(const std::string& renderPass, const std::string& name);

    void setBufferDataImmediately(Buffer&, const std::byte* data, size_t size, size_t offset = 0);

    const std::vector<Buffer>& buffers() const;
    const std::vector<Texture>& textures() const;
    const std::vector<RenderTarget>& renderTargets() const;
    const std::vector<RenderState>& renderStates() const;
    const std::vector<BufferUpdate>& bufferUpdates() const;
    const std::vector<TextureUpdateFromFile>& textureUpdates() const;

    Badge<ResourceManager> exchangeBadges(Badge<Backend>) const;

protected:
    std::string makeQualifiedName(const std::string& pass, const std::string& name);

private:
    std::optional<std::string> m_current_pass_name;

    const RenderTarget* m_windowRenderTarget;

    std::unordered_map<std::string, const Buffer*> m_name_buffer_map;
    std::unordered_map<std::string, const Texture*> m_name_texture_map;
    std::unordered_map<std::string, const RenderTarget*> m_name_render_target_map;

    std::vector<BufferUpdate> m_immediate_buffer_updates;
    std::vector<TextureUpdateFromFile> m_immediate_texture_updates;

    static constexpr int maxNumBuffers { 10 };
    CapList<Buffer> m_buffers { maxNumBuffers };

    static constexpr int maxNumTextures { 10 };
    CapList<Texture> m_textures { maxNumTextures };

    static constexpr int maxNumRenderTargets { 4 };
    CapList<RenderTarget> m_renderTargets { maxNumRenderTargets };

    static constexpr int maxNumRenderStates { 10 };
    CapList<RenderState> m_renderStates { maxNumRenderStates };
};

template<typename T>
[[nodiscard]] Buffer& ResourceManager::createBuffer(std::vector<T>&& inData, Buffer::Usage usage, Buffer::MemoryHint memoryHint)
{
    size_t dataSize = inData.size() * sizeof(T);
    auto* binaryData = reinterpret_cast<const std::byte*>(inData.data());
    ASSERT(binaryData != nullptr);
    return createBuffer(binaryData, dataSize, usage, memoryHint);
}
