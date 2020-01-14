#pragma once

#include "ApplicationState.h"
#include "ResourceChange.h"
#include "Resources.h"
#include "utility/util.h"
#include <unordered_map>
#include <array>

class ResourceManager {
public:
    explicit ResourceManager();

    void setCurrentPass(std::string);

    // TODO: Handle the fact that IDs are updated in the background and since we are taking copies of Resources everywhere this system is
    //  currently kind of broken! We need to use references or something like that, without making it too cumbersome for the Apps to manage.

    [[nodiscard]] RenderTarget& getWindowRenderTarget();
    [[nodiscard]] RenderTarget& createRenderTarget(std::initializer_list<RenderTarget::Attachment>);

    [[nodiscard]] Texture2D& loadTexture2D(std::string imagePath, bool srgb, bool generateMipmaps);
    [[nodiscard]] Texture2D& createTexture2D(int width, int height, Texture2D::Format);

    [[nodiscard]] Buffer& createBuffer(size_t size, Buffer::Usage, Buffer::MemoryHint);
    template<typename T>
    [[nodiscard]] Buffer& createBuffer(std::vector<T>&& inData, Buffer::Usage usage, Buffer::MemoryHint);
    [[nodiscard]] Buffer& createBuffer(const std::byte* data, size_t size, Buffer::Usage, Buffer::MemoryHint);

    [[nodiscard]] RenderState& createRenderState(const RenderTarget&, const VertexLayout&, const Shader&, const ShaderBindingSet&, const Viewport&, const BlendState&, const RasterState&);

    void publish(const std::string& name, const Buffer&);
    void publish(const std::string& name, const Texture2D&);
    void publish(const std::string& name, const RenderTarget&);

    [[nodiscard]] const Texture2D* getTexture2D(const std::string& renderPass, const std::string& name);

    void setBufferDataImmediately(Buffer&, const std::byte* data, size_t size, size_t offset = 0);

    const std::vector<Buffer>& buffers() const;
    const std::vector<Texture2D>& textures() const;
    const std::vector<RenderTarget>& renderTargets() const;
    const std::vector<RenderState>& renderStates() const;
    const std::vector<BufferUpdate>& bufferUpdates() const;
    const std::vector<TextureUpdateFromFile>& textureUpdates() const;

protected:
    std::string makeQualifiedName(const std::string& pass, const std::string& name);

private:
    std::optional<std::string> m_current_pass_name;

    std::unordered_map<std::string, const Buffer*> m_name_buffer_map;
    std::unordered_map<std::string, const Texture2D*> m_name_texture_map;
    std::unordered_map<std::string, const RenderTarget*> m_name_render_target_map;

    std::vector<BufferUpdate> m_immediate_buffer_updates;
    std::vector<TextureUpdateFromFile> m_immediate_texture_updates;

    // TODO: Maybe we want some better way of handling these capped vectors? Custom class?
    static constexpr int maxNumBuffers { 10 };
    std::vector<Buffer> m_buffers;

    static constexpr int maxNumTextures { 10 };
    std::vector<Texture2D> m_textures;

    static constexpr int maxNumRenderTargets { 4 };
    std::vector<RenderTarget> m_renderTargets;

    static constexpr int maxNumRenderStates { 10 };
    std::vector<RenderState> m_renderStates;
};

template<typename T>
[[nodiscard]] Buffer& ResourceManager::createBuffer(std::vector<T>&& inData, Buffer::Usage usage, Buffer::MemoryHint memoryHint)
{
    size_t dataSize = inData.size() * sizeof(T);
    auto* binaryData = reinterpret_cast<const std::byte*>(inData.data());
    ASSERT(binaryData != nullptr);
    return createBuffer(binaryData, dataSize, usage, memoryHint);
}
