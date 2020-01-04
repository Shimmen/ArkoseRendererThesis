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

Texture2D::Texture2D(Badge<ResourceManager>, int width, int height, Format format, MinFilter minFilter, MagFilter magFilter)
    : m_extent(width, height)
    , m_format(format)
    , m_minFilter(minFilter)
    , m_magFilter(magFilter)
{
}

bool Texture2D::hasMipmaps() const
{
    // TODO: Use min filter to figure out the answer!
    return false;
}

RenderTarget::RenderTarget(Badge<ResourceManager>, Texture2D&& colorTexture)
    : m_attachments {}
{
    Attachment colorAttachment = { .type = AttachmentType::Color0, .texture = &colorTexture };
    m_attachments.push_back(colorAttachment);
}

RenderTarget::RenderTarget(Badge<ResourceManager>, std::initializer_list<Attachment> attachments)
    : m_attachments {}
{
    for (const Attachment& attachment : attachments) {
        if (attachment.type == AttachmentType::Depth) {
            m_depthAttachment = attachment;
        } else {
            m_attachments.emplace_back(attachment);
        }
    }

    if (totalAttachmentCount() < 1) {
        LogErrorAndExit("RenderTarget error: tried to create with less than one attachments!\n");
    }

    if (colorAttachmentCount() < 1) {
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

    if (m_depthAttachment.has_value() && m_depthAttachment->texture->extent() != firstExtent) {
        LogErrorAndExit("RenderTarget error: tried to create with depth attachments of non-matching size: (%ix%i) vs (%ix%i)\n",
            m_depthAttachment->texture->extent().width(), m_depthAttachment->texture->extent().height(),
            firstExtent.width(), firstExtent.height());
    }
}

const Extent2D& RenderTarget::extent() const
{
    return m_attachments.front().texture->extent();
}

size_t RenderTarget::colorAttachmentCount() const
{
    return m_attachments.size();
}

size_t RenderTarget::totalAttachmentCount() const
{
    return colorAttachmentCount() + (hasDepthAttachment() ? 1 : 0);
}

bool RenderTarget::hasDepthAttachment() const
{
    return m_depthAttachment.has_value();
}

bool RenderTarget::isWindowTarget() const
{
    return m_isWindowTarget;
}

Buffer::Buffer(Badge<ResourceManager>, size_t size, Usage usage, MemoryHint memoryHint)
    : m_size(size)
    , m_usage(usage)
    , m_memoryHint(memoryHint)
{
}
