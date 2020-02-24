#include "Registry.h"

#include "utility/FileIO.h"
#include "utility/Logging.h"
#include "utility/util.h"
#include <stb_image.h>

Registry::Registry(const RenderTarget* windowRenderTarget)
    : m_windowRenderTarget(windowRenderTarget)
{
}

void Registry::setCurrentNode(std::string node)
{
    m_currentNodeName = std::move(node);
}

const RenderTarget& Registry::windowRenderTarget()
{
    ASSERT(m_windowRenderTarget);
    return *m_windowRenderTarget;
}

RenderTarget& Registry::createRenderTarget(std::initializer_list<RenderTarget::Attachment> attachments)
{
    RenderTarget renderTarget { {}, attachments };
    m_renderTargets.push_back(renderTarget);
    return m_renderTargets.back();
}

Texture& Registry::createTexture2D(Extent2D extent, Texture::Format format, Texture::Usage usage)
{
    Texture texture { {}, extent, format, usage, Texture::MinFilter::Linear, Texture::MagFilter::Linear, Texture::Mipmap::None };
    m_textures.push_back(texture);
    return m_textures.back();
}

Buffer& Registry::createBuffer(size_t size, Buffer::Usage usage, Buffer::MemoryHint memoryHint)
{
    ASSERT(size > 0);
    Buffer buffer = { {}, size, usage, memoryHint };
    m_buffers.push_back(buffer);
    return m_buffers.back();
}

Buffer& Registry::createBuffer(const std::byte* data, size_t size, Buffer::Usage usage, Buffer::MemoryHint memoryHint)
{
    Buffer& buffer = createBuffer(size, usage, memoryHint);

    std::vector<std::byte> data_copy { data, data + size };
    m_immediateBufferUpdates.emplace_back(buffer, std::move(data_copy));

    return buffer;
}

BindingSet& Registry::createBindingSet(std::initializer_list<ShaderBinding> shaderBindings)
{
    BindingSet set = { {}, shaderBindings };
    m_shaderBindingSets.push_back(set);
    return m_shaderBindingSets.back();
}

Texture& Registry::createPixelTexture(vec4 pixelValue, bool srgb)
{
    auto format = srgb
        ? Texture::Format::sRGBA8
        : Texture::Format::RGBA8;
    auto usage = Texture::Usage::Sampled;

    m_textures.push_back({ {}, { 1, 1 }, format, usage, Texture::MinFilter::Nearest, Texture::MagFilter::Nearest, Texture::Mipmap::None });
    Texture& texture = m_textures.back();

    m_immediateTextureUpdates.emplace_back(texture, pixelValue);

    return texture;
}

Texture& Registry::loadTexture2D(const std::string& imagePath, bool srgb, bool generateMipmaps)
{
    if (!FileIO::isFileReadable(imagePath)) {
        LogErrorAndExit("Could not read image at path '%s'.\n", imagePath.c_str());
    }

    int width, height, componentCount;
    stbi_info(imagePath.c_str(), &width, &height, &componentCount);

    Texture::Format format;
    switch (componentCount) {
    case 3:
        // NOTE: sRGB (without alpha) is often not supported, so we don't support it in ArkoseRenderer
        format = (srgb) ? Texture::Format::sRGBA8 : Texture::Format::RGB8;

        // TODO: I think this is just temporary.. why is that not supported??
        if (format == Texture::Format::RGB8) {
            format = Texture::Format::RGBA8;
        }
        break;
    case 4:
        format = (srgb) ? Texture::Format::sRGBA8 : Texture::Format::RGBA8;
        break;
    default:
        LogErrorAndExit("Currently no support for other than (s)RGB and (s)RGBA texture loading!\n");
    }

    // TODO: Maybe we want to allow more stuff..?
    auto usage = Texture::Usage::Sampled;

    auto mipmapMode = generateMipmaps ? Texture::Mipmap::Linear : Texture::Mipmap::None;

    m_textures.push_back({ {}, { width, height }, format, usage, Texture::MinFilter::Linear, Texture::MagFilter::Linear, mipmapMode });
    Texture& texture = m_textures.back();

    m_immediateTextureUpdates.emplace_back(texture, imagePath, generateMipmaps);

    return texture;
}

RenderState& Registry::createRenderState(const RenderStateBuilder& builder)
{
    return createRenderState(builder.renderTarget, builder.vertexLayout, builder.shader,
                             builder.bindingSets(), builder.viewport(), builder.blendState(), builder.rasterState());
}

RenderState& Registry::createRenderState(
    const RenderTarget& renderTarget, const VertexLayout& vertexLayout,
    const Shader& shader, std::vector<const BindingSet*> shaderBindingSets,
    const Viewport& viewport, const BlendState& blendState, const RasterState& rasterState)
{
    RenderState renderState = { {}, renderTarget, vertexLayout, shader, shaderBindingSets, viewport, blendState, rasterState };
    m_renderStates.push_back(renderState);
    return m_renderStates.back();
}

BottomLevelAS& Registry::createBottomLevelAccelerationStructure(std::vector<RTGeometry> geometries)
{
    BottomLevelAS blas = { {}, geometries };
    m_bottomLevelAS.push_back(blas);
    return m_bottomLevelAS.back();
}

TopLevelAS& Registry::createTopLevelAccelerationStructure(std::vector<RTGeometryInstance> instances)
{
    TopLevelAS tlas = { {}, instances };
    m_topLevelAS.push_back(tlas);
    return m_topLevelAS.back();
}

RayTracingState& Registry::createRayTracingState(const std::vector<ShaderFile>& shaderBindingTable, std::vector<const BindingSet*> bindingSets)
{
    RayTracingState rtState = { {}, shaderBindingTable, bindingSets, 1 };
    m_rayTracingStates.push_back(rtState);
    return m_rayTracingStates.back();
}

void Registry::publish(const std::string& name, const Buffer& buffer)
{
    ASSERT(m_currentNodeName.has_value());
    std::string fullName = makeQualifiedName(m_currentNodeName.value(), name);
    auto entry = m_nameBufferMap.find(fullName);
    ASSERT(entry == m_nameBufferMap.end());
    m_nameBufferMap[fullName] = &buffer;
}

void Registry::publish(const std::string& name, const Texture& texture)
{
    ASSERT(m_currentNodeName.has_value());
    std::string fullName = makeQualifiedName(m_currentNodeName.value(), name);
    auto entry = m_nameTextureMap.find(fullName);
    ASSERT(entry == m_nameTextureMap.end());
    m_nameTextureMap[fullName] = &texture;
}

const Texture* Registry::getTexture(const std::string& renderPass, const std::string& name)
{
    std::string fullName = makeQualifiedName(renderPass, name);
    auto entry = m_nameTextureMap.find(fullName);

    if (entry == m_nameTextureMap.end()) {
        return nullptr;
    }

    ASSERT(m_currentNodeName.has_value());
    NodeDependency dependency { m_currentNodeName.value(), renderPass };
    m_nodeDependencies.insert(dependency);

    const Texture* texture = entry->second;
    return texture;
}

const Buffer* Registry::getBuffer(const std::string& renderPass, const std::string& name)
{
    std::string fullName = makeQualifiedName(renderPass, name);
    auto entry = m_nameBufferMap.find(fullName);

    if (entry == m_nameBufferMap.end()) {
        return nullptr;
    }

    ASSERT(m_currentNodeName.has_value());
    NodeDependency dependency { m_currentNodeName.value(), renderPass };
    m_nodeDependencies.insert(dependency);

    const Buffer* buffer = entry->second;
    return buffer;
}

const std::unordered_set<NodeDependency>& Registry::nodeDependencies() const
{
    return m_nodeDependencies;
}

const std::vector<Buffer>& Registry::buffers() const
{
    return m_buffers.vector();
}

const std::vector<Texture>& Registry::textures() const
{
    return m_textures.vector();
}

const std::vector<RenderTarget>& Registry::renderTargets() const
{
    return m_renderTargets.vector();
}

const std::vector<BindingSet>& Registry::bindingSets() const
{
    return m_shaderBindingSets.vector();
}

const std::vector<RenderState>& Registry::renderStates() const
{
    return m_renderStates.vector();
}

const std::vector<BottomLevelAS>& Registry::bottomLevelAS() const
{
    return m_bottomLevelAS.vector();
}

const std::vector<TopLevelAS>& Registry::topLevelAS() const
{
    return m_topLevelAS.vector();
}

const std::vector<RayTracingState>& Registry::rayTracingStates() const
{
    return m_rayTracingStates.vector();
}

const std::vector<BufferUpdate>& Registry::bufferUpdates() const
{
    return m_immediateBufferUpdates;
}

const std::vector<TextureUpdate>& Registry::textureUpdates() const
{
    return m_immediateTextureUpdates;
}

Badge<Registry> Registry::exchangeBadges(Badge<Backend>) const
{
    return {};
}

std::string Registry::makeQualifiedName(const std::string& node, const std::string& name)
{
    return node + ':' + name;
}
