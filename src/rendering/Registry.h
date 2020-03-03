#pragma once

#include "AppState.h"
#include "NodeDependency.h"
#include "ResourceChange.h"
#include "Resources.h"
#include "utility/CapList.h"
#include "utility/util.h"
#include <unordered_map>
#include <unordered_set>

class Registry {
public:
    explicit Registry(const RenderTarget* windowRenderTarget = nullptr);

    void setCurrentNode(std::string);

    [[nodiscard]] const RenderTarget& windowRenderTarget();
    [[nodiscard]] RenderTarget& createRenderTarget(std::initializer_list<RenderTarget::Attachment>);

    [[nodiscard]] Texture& createPixelTexture(vec4 pixelValue, bool srgb);
    [[nodiscard]] Texture& loadTexture2D(const std::string& imagePath, bool srgb, bool generateMipmaps);
    [[nodiscard]] Texture& createTexture2D(Extent2D, Texture::Format, Texture::Usage);

    [[nodiscard]] Buffer& createBuffer(size_t size, Buffer::Usage, Buffer::MemoryHint);
    template<typename T>
    [[nodiscard]] Buffer& createBuffer(std::vector<T>&& inData, Buffer::Usage usage, Buffer::MemoryHint);
    [[nodiscard]] Buffer& createBuffer(const std::byte* data, size_t size, Buffer::Usage, Buffer::MemoryHint);

    [[nodiscard]] BindingSet& createBindingSet(std::initializer_list<ShaderBinding>);

    [[nodiscard]] RenderState& createRenderState(const RenderStateBuilder&);
    [[nodiscard]] RenderState& createRenderState(const RenderTarget&, const VertexLayout&, const Shader&, std::vector<const BindingSet*>, const Viewport&, const BlendState&, const RasterState&, const DepthState&);

    [[nodiscard]] BottomLevelAS& createBottomLevelAccelerationStructure(std::vector<RTGeometry>);
    [[nodiscard]] TopLevelAS& createTopLevelAccelerationStructure(std::vector<RTGeometryInstance>);

    [[nodiscard]] RayTracingState& createRayTracingState(const std::vector<ShaderFile>& shaderBindingTable, std::vector<const BindingSet*>, uint32_t maxRecursionDepth);

    void publish(const std::string& name, const Buffer&);
    void publish(const std::string& name, const Texture&);

    [[nodiscard]] const Texture* getTexture(const std::string& renderPass, const std::string& name);
    [[nodiscard]] const Buffer* getBuffer(const std::string& renderPass, const std::string& name);

    [[nodiscard]] const std::unordered_set<NodeDependency>& nodeDependencies() const;

    [[nodiscard]] const std::vector<Buffer>& buffers() const;
    [[nodiscard]] const std::vector<Texture>& textures() const;
    [[nodiscard]] const std::vector<RenderTarget>& renderTargets() const;
    [[nodiscard]] const std::vector<BindingSet>& bindingSets() const;
    [[nodiscard]] const std::vector<RenderState>& renderStates() const;
    [[nodiscard]] const std::vector<BottomLevelAS>& bottomLevelAS() const;
    [[nodiscard]] const std::vector<TopLevelAS>& topLevelAS() const;
    [[nodiscard]] const std::vector<RayTracingState>& rayTracingStates() const;
    [[nodiscard]] const std::vector<BufferUpdate>& bufferUpdates() const;
    [[nodiscard]] const std::vector<TextureUpdate>& textureUpdates() const;

    [[nodiscard]] Badge<Registry> exchangeBadges(Badge<Backend>) const;

protected:
    std::string makeQualifiedName(const std::string& node, const std::string& name);

private:
    std::optional<std::string> m_currentNodeName;
    std::unordered_set<NodeDependency> m_nodeDependencies;

    const RenderTarget* m_windowRenderTarget;

    std::unordered_map<std::string, const Buffer*> m_nameBufferMap;
    std::unordered_map<std::string, const Texture*> m_nameTextureMap;

    std::vector<BufferUpdate> m_immediateBufferUpdates;
    std::vector<TextureUpdate> m_immediateTextureUpdates;

    // TODO: Maybe just replace all of this below with a large shared memory arena?

    static constexpr int maxNumBuffers { 10000 };
    CapList<Buffer> m_buffers { maxNumBuffers };

    static constexpr int maxNumTextures { 10000 };
    CapList<Texture> m_textures { maxNumTextures };

    static constexpr int maxNumRenderTargets { 4 };
    CapList<RenderTarget> m_renderTargets { maxNumRenderTargets };

    static constexpr int maxNumShaderBindingSets { 1280 };
    CapList<BindingSet> m_shaderBindingSets { maxNumShaderBindingSets };

    static constexpr int maxNumRenderStates { 10 };
    CapList<RenderState> m_renderStates { maxNumRenderStates };

    static constexpr int maxNumBottomLevelAS { 1000 };
    CapList<BottomLevelAS> m_bottomLevelAS { maxNumBottomLevelAS };

    static constexpr int maxNumTopLevelAS { 10 };
    CapList<TopLevelAS> m_topLevelAS { maxNumTopLevelAS };

    static constexpr int maxNumRayTracingStates { 10 };
    CapList<RayTracingState> m_rayTracingStates { maxNumRayTracingStates };
};

template<typename T>
[[nodiscard]] Buffer& Registry::createBuffer(std::vector<T>&& inData, Buffer::Usage usage, Buffer::MemoryHint memoryHint)
{
    size_t dataSize = inData.size() * sizeof(T);
    auto* binaryData = reinterpret_cast<const std::byte*>(inData.data());
    ASSERT(binaryData != nullptr);
    return createBuffer(binaryData, dataSize, usage, memoryHint);
}
