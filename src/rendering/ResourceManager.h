#pragma once

#include "ApplicationState.h"
#include "ResourceChange.h"
#include "Resources.h"
#include "utility/util.h"
#include <unordered_map>

class Backend;

class ResourceManager {
    // TODO: This is passed to the construct passes and is used to allocate
    //  resources (e.g. textures & buffers) used in the execute pass.
public:
    explicit ResourceManager(int frameAssociation);

    void setCurrentPass(std::string);

    // TODO: Add a nice API for creating & managing resources here

    // TODO: Idea for implementation: remember all previous resources created from it (from previous frames)
    //  and if everything is the same, don't actually delete and construct new resources that are the same.
    //  Example situation: the window resizes, the render pass is reconstructed, and the code requests an
    //  identical buffer for e.g. static data, so we simply return the last used one. Maybe we then only delete
    //  resources when we ask for new stuff or when you manually request that things are release (e.g. at shutdown).
    //  Internally a backend could keep track of <RenderPass, ResourceManager> pairs which are in sync with each other!

    // TODO: Let's say we create a new ResourceManager for each render pass and frame, then after a pass has submitted
    //  it we could call some transferUnchangedResources(oldManager, newManager) so the old stuff isn't deleted etc.

    // TODO: Another idea for implementation! Since all other resources go through this resource builder
    //  (really it should be called ResourceManager) why not manage a "static" resource manager for an App object,
    //  which manages all resources that persist throughout the whole application lifetime.

    [[nodiscard]] RenderTarget getWindowRenderTarget();
    [[nodiscard]] RenderTarget createRenderTarget(std::initializer_list<RenderTarget::Attachment>);

    [[nodiscard]] Texture2D loadTexture2D(std::string imagePath, bool srgb, bool generateMipmaps);
    [[nodiscard]] Texture2D createTexture2D(int width, int height, Texture2D::Format);
    [[nodiscard]] std::optional<Texture2D> getTexture2D(const std::string& renderPass, const std::string& name);

    [[nodiscard]] Buffer createBuffer(size_t size, Buffer::Usage);
    template<typename T>
    [[nodiscard]] Buffer createBuffer(std::vector<T>&& inData, Buffer::Usage usage);
    [[nodiscard]] Buffer createBuffer(const std::byte* data, size_t size, Buffer::Usage);

    void assignName(const std::string& name, const Buffer&);
    void assignName(const std::string& name, const Texture2D&);
    void assignName(const std::string& name, const RenderTarget&);

    void setBufferDataImmediately(const Buffer&, const std::byte* data, size_t size, size_t offset = 0);


    const std::vector<Buffer>& buffers() const;
    const std::vector<Texture2D>& textures() const;
    const std::vector<RenderTarget>& renderTargets() const;
    const std::vector<BufferUpdate>& bufferUpdates() const;
    const std::vector<TextureUpdateFromFile>& textureUpdates() const;

protected:
    std::string makeQualifiedName(const std::string& pass, const std::string& name);

private:
    int m_frameAssociation;

    std::optional<std::string> m_current_pass_name;

    std::unordered_map<std::string, Buffer> m_name_buffer_map;
    std::unordered_map<std::string, Texture2D> m_name_texture_map;
    std::unordered_map<std::string, RenderTarget> m_name_render_target_map;

    std::vector<BufferUpdate> m_immediate_buffer_updates;
    std::vector<TextureUpdateFromFile> m_immediate_texture_updates;

    std::vector<Buffer> m_buffers;
    std::vector<Texture2D> m_textures;
    std::vector<RenderTarget> m_renderTargets;
};

template<typename T>
[[nodiscard]] Buffer ResourceManager::createBuffer(std::vector<T>&& inData, Buffer::Usage usage)
{
    size_t dataSize = inData.size() * sizeof(T);
    auto* binaryData = reinterpret_cast<const std::byte*>(inData.data());
    ASSERT(binaryData != nullptr);
    return createBuffer(binaryData, dataSize, usage);
}
