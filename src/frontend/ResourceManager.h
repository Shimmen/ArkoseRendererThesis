#pragma once

#include "ApplicationState.h"
#include "Resources.h"

class Backend;

class ResourceManager {
    // TODO: This is passed to the construct passes and is used to allocate
    //  resources (e.g. textures & buffers) used in the execute pass.
public:
    explicit ResourceManager(ApplicationState, Backend& backend);

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

    [[nodiscard]] Texture2D loadTexture2D(std::string imagePath, bool generateMipmaps);
    [[nodiscard]] Texture2D createTexture2D(int width, int height, Texture2D::Components, bool mipmaps, bool srgb);
    [[nodiscard]] Texture2D getTexture2D(std::string renderPass, std::string name);

    [[nodiscard]] Buffer createBuffer(size_t size, Buffer::Usage);
    [[nodiscard]] Buffer createBuffer(const void* data, size_t size, Buffer::Usage);

    template<typename T>
    [[nodiscard]] Buffer createBuffer(const std::vector<T>& inData, Buffer::Usage usage)
    {
        size_t dataSize = inData.size() * sizeof(T);
        return createBuffer(inData.data(), dataSize, usage);
    }

    void setBufferDataImmediately(const Buffer&, const void* data, size_t size, size_t offset = 0);

private:
    const ApplicationState m_appState;
    Backend& m_backend;
    // TODO: Add some type of references to resources here so it can keep track of stuff.
};
