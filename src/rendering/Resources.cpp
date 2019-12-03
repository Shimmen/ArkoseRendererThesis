#include "Resources.h"

#include "rendering/ShaderManager.h"
#include "utility/logging.h"
#include "utility/util.h"

uint64_t Resource::id() const
{
    if (m_id == UINT64_MAX) {
        LogErrorAndExit("Requested resource does not have an attached backend!\n");
    }
    return m_id;
}

void Resource::registerBackend(uint64_t id)
{
    ASSERT(id != UINT64_MAX);
    if (m_id != UINT64_MAX) {
        LogErrorAndExit("Trying to register backend for a resource twice!\n");
    }
    m_id = id;
}

Texture2D::Texture2D(Badge<ResourceManager>, int width, int height, Components components, bool srgb, bool mipmaps)
    : m_extent(width, height)
    , m_components(components)
    , m_srgb(srgb)
    , m_mipmaps(mipmaps)
{
}

RenderTarget::RenderTarget(Badge<ResourceManager>, Texture2D&& colorTexture)
    : m_attachments()
{
    Attachment colorAttachment = { .type = AttachmentType::Color0, .texture = &colorTexture };
    m_attachments.push_back(colorAttachment);
}

RenderTarget::RenderTarget(Badge<ResourceManager>, std::initializer_list<Attachment> targets)
{
    for (const Attachment& attachment : targets) {
        if (attachment.type == AttachmentType::Depth) {
            m_depthAttachment = attachment;
        } else {
            m_attachments.emplace_back(attachment);
        }
    }

    if (attachmentCount() < 1) {
        LogErrorAndExit("RenderTarget error: tried to create with less than one color attachments!\n");
    }
    /*
    Extent2D firstExtent = m_attachments.front().texture->extent;
    for (auto& attachment : m_attachments) {
        if (attachment.texture->extent != firstExtent) {
            LogErrorAndExit("RenderTarget error: tried to create with attachments of different sizes: (%ix%i) vs (%ix%i)\n",
                attachment.texture->extent.width(), attachment.texture->extent.height(),
                firstExtent.width(), firstExtent.height());
        }
    }

    if (m_depthAttachment.has_value() && m_depthAttachment->texture->extent != firstExtent) {
        LogErrorAndExit("RenderTarget error: tried to create with depth attachments of non-matching size: (%ix%i) vs (%ix%i)\n",
            m_depthAttachment->texture->extent.width(), m_depthAttachment->texture->extent.height(),
            firstExtent.width(), firstExtent.height());
    }
    */
}

const Extent2D& RenderTarget::extent() const
{
    return m_attachments.front().texture->extent();
}

size_t RenderTarget::attachmentCount() const
{
    return m_attachments.size();
}

bool RenderTarget::hasDepthAttachment() const
{
    return m_depthAttachment.has_value();
}

bool RenderTarget::isWindowTarget() const
{
    return m_isWindowTarget;
}

Buffer::Buffer(Badge<ResourceManager>, size_t size, Usage usage)
    : m_size(size)
    , m_usage(usage)
{
}

ShaderFile::ShaderFile(std::string name, ShaderFileType type)
    : m_name(std::move(name))
    , m_type(type)
{
    auto& manager = ShaderManager::instance();
    switch (manager.loadAndCompileImmediately(name)) {
    case ShaderManager::ShaderStatus::FileNotFound:
        LogErrorAndExit("Shader file '%s' not found, exiting.\n");
    case ShaderManager::ShaderStatus::CompileError: {
        std::string errorMessage = manager.shaderError(name).value();
        LogError("Shader file '%s' has compile errors:\n");
        LogError("  %s\n", errorMessage.c_str());
        LogErrorAndExit("Exiting due to bad shader at startup.\n");
    }
    default:
        break;
    }
}

ShaderFileType ShaderFile::type() const
{
    return m_type;
}

Shader Shader::createBasic(std::string name, std::string vertexName, std::string fragmentName)
{
    ShaderFile vertexFile { std::move(vertexName), ShaderFileType::Vertex };
    ShaderFile fragmentFile { std::move(fragmentName), ShaderFileType::Fragment };
    return Shader(std::move(name), { vertexFile, fragmentFile }, ShaderType::Raster);
}

Shader Shader::createCompute(std::string name, std::string computeName)
{
    ShaderFile computeFile { std::move(computeName), ShaderFileType::Compute };
    return Shader(std::move(name), { computeFile }, ShaderType::Compute);
}

Shader::Shader(std::string name, std::vector<ShaderFile> files, ShaderType type)
    : m_name(std::move(name))
    , m_files(std::move(files))
    , m_type(type)
{
}

Shader::~Shader()
{
    if (m_handle != NullHandle) {
        // TODO: Delete the shader and etc stuff
    }
}

ShaderType Shader::type() const
{
    return m_type;
}
