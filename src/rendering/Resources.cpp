#include "Resources.h"

#include "utility/logging.h"
#include "utility/util.h"

uint64_t Resource::id() const
{
    if (m_id == Resource::NullId) {
        LogErrorAndExit("Requested resource does not have an attached backend!\n");
    }
    return m_id;
}

bool Resource::hasBackend() const
{
    return m_id != Resource::NullId;
}

void Resource::unregisterBackend(Badge<Backend>) const
{
    m_id = Resource::NullId;
}

void Resource::registerBackend(Badge<Backend>, uint64_t id) const
{
    ASSERT(id != UINT64_MAX);
    if (m_id != UINT64_MAX) {
        LogErrorAndExit("Trying to register backend for a resource twice!\n");
    }
    m_id = id;
}

Texture::Texture(Badge<ResourceManager>, Extent2D extent, Format format, Usage usage, MinFilter minFilter, MagFilter magFilter)
    : m_extent(extent)
    , m_format(format)
    , m_usage(usage)
    , m_minFilter(minFilter)
    , m_magFilter(magFilter)
{
}

bool Texture::hasMipmaps() const
{
    // TODO: Use min filter to figure out the answer!
    return false;
}

RenderTarget::RenderTarget(Badge<ResourceManager>, Texture& colorTexture)
    : m_attachments {}
{
    Attachment colorAttachment = { .type = AttachmentType::Color0, .texture = &colorTexture };
    m_attachments.push_back(colorAttachment);
}

RenderTarget::RenderTarget(Badge<ResourceManager>, std::initializer_list<Attachment> attachments)
    : m_attachments {}
{
    // TODO: This is all very messy and could probably be cleaned up a fair bit!

    for (const Attachment& attachment : attachments) {
        m_attachments.emplace_back(attachment);
    }

    if (totalAttachmentCount() < 1) {
        LogErrorAndExit("RenderTarget error: tried to create with less than one attachments!\n");
    }

    for (auto& [_, texture] : m_attachments) {
        if (texture->usage() != Texture::Usage::Attachment && texture->usage() != Texture::Usage::All) {
            LogErrorAndExit("RenderTarget error: tried to create with texture that can't be used as attachment\n");
        }
    }

    if (totalAttachmentCount() < 2) {
        return;
    }

    Extent2D firstExtent = m_attachments.front().texture->extent();

    for (auto& attachment : m_attachments) {
        if (attachment.texture->extent() != firstExtent) {
            LogErrorAndExit("RenderTarget error: tried to create with attachments of different sizes: (%ix%i) vs (%ix%i)\n",
                attachment.texture->extent().width(), attachment.texture->extent().height(),
                firstExtent.width(), firstExtent.height());
        }
    }

    // Keep attachments sorted from Color0, Color1, .. ColorN, Depth
    std::sort(m_attachments.begin(), m_attachments.end(), [](const Attachment& left, const Attachment& right) {
        return left.type < right.type;
    });

    // Make sure we don't have duplicated attachment types & that the color attachments aren't sparse
    if (m_attachments.front().type != AttachmentType::Depth && m_attachments.front().type != AttachmentType::Color0) {
        LogErrorAndExit("RenderTarget error: sparse color attachments in render target\n");
    }
    std::optional<AttachmentType> lastType {};
    for (auto& attachment : m_attachments) {
        if (lastType.has_value()) {
            if (attachment.type == lastType.value()) {
                LogErrorAndExit("RenderTarget error: duplicate attachment types in render target\n");
            }
            if (attachment.type != AttachmentType::Depth) {
                auto lastVal = static_cast<unsigned int>(attachment.type);
                auto currVal = static_cast<unsigned int>(lastType.value());
                if (currVal != lastVal + 1) {
                    LogErrorAndExit("RenderTarget error: sparse color attachments in render target\n");
                }
            }
        }
    }
}

const Extent2D& RenderTarget::extent() const
{
    return m_attachments.front().texture->extent();
}

size_t RenderTarget::colorAttachmentCount() const
{
    size_t total = totalAttachmentCount();
    if (hasDepthAttachment()) {
        return total - 1;
    } else {
        return total;
    }
}

size_t RenderTarget::totalAttachmentCount() const
{
    return m_attachments.size();
}

bool RenderTarget::hasDepthAttachment() const
{
    if (m_attachments.empty()) {
        return false;
    }

    const Attachment& last = m_attachments.back();
    return last.type == AttachmentType::Depth;
}

const Texture* RenderTarget::attachment(AttachmentType requestedType) const
{
    for (const auto& [type, texture] : m_attachments) {
        if (type == requestedType) {
            return texture;
        }
    }
    return nullptr;
}

const std::vector<RenderTarget::Attachment>& RenderTarget::sortedAttachments() const
{
    return m_attachments;
}

Buffer::Buffer(Badge<ResourceManager>, size_t size, Usage usage, MemoryHint memoryHint)
    : m_size(size)
    , m_usage(usage)
    , m_memoryHint(memoryHint)
{
}

ShaderBinding::ShaderBinding(uint32_t index, ShaderStage shaderStage, const Buffer* buffer)
    : bindingIndex(index)
    , shaderStage(shaderStage)
    , type(ShaderBindingType::UniformBuffer) // TODO: Technically this could be some other type of buffer here (e.g. shader storage buffer)
    , buffer(buffer)
    , texture(nullptr)
{
}

ShaderBinding::ShaderBinding(uint32_t index, ShaderStage shaderStage, const Texture* texture)
    : bindingIndex(index)
    , shaderStage(shaderStage)
    , type(ShaderBindingType::TextureSampler)
    , buffer(nullptr)
    , texture(texture)
{
    if (!texture) {
        LogErrorAndExit("ShaderBinding error: null texture\n");
    }
    if (texture->usage() != Texture::Usage::Sampled && texture->usage() != Texture::Usage::All) {
        LogErrorAndExit("ShaderBinding error: texture does not support sampling\n");
    }
}

ShaderBindingSet::ShaderBindingSet(std::initializer_list<ShaderBinding> list)
    : m_shaderBindings(list)
{
    std::sort(m_shaderBindings.begin(), m_shaderBindings.end(), [](const ShaderBinding& left, const ShaderBinding& right) {
        return left.bindingIndex < right.bindingIndex;
    });

    for (int i = 0; i < m_shaderBindings.size() - 1; ++i) {
        if (m_shaderBindings[i].bindingIndex == m_shaderBindings[i + 1].bindingIndex) {
            LogErrorAndExit("ShaderBindingSet error: duplicate bindings\n");
        }
    }
}

const std::vector<ShaderBinding>& ShaderBindingSet::shaderBindings() const
{
    return m_shaderBindings;
}
