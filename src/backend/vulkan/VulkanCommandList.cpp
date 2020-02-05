#include "VulkanCommandList.h"

#include "VulkanBackend.h"

VulkanCommandList::VulkanCommandList(VulkanBackend& backend, VkCommandBuffer commandBuffer)
    : m_backend(backend)
    , m_commandBuffer(commandBuffer)
{
}

void VulkanCommandList::updateBuffer(Buffer& buffer, void* data, size_t size)
{
    auto& bufInfo = m_backend.bufferInfo(buffer);

    switch (buffer.memoryHint()) {
    case Buffer::MemoryHint::GpuOptimal:
        if (!m_backend.setBufferDataUsingStagingBuffer(bufInfo.buffer, data, size, &m_commandBuffer)) {
            LogError("updateBuffer(): could not update the buffer memory through staging buffer.\n");
        }
        break;
    case Buffer::MemoryHint::TransferOptimal:
        if (!m_backend.setBufferMemoryUsingMapping(bufInfo.allocation, data, size)) {
            LogError("updateBuffer(): could not update the buffer memory through mapping.\n");
        }
        break;
    case Buffer::MemoryHint::GpuOnly:
        LogError("updateBuffer(): can't update buffer with GpuOnly memory hint, ignoring\n");
        break;
    }
}

void VulkanCommandList::setRenderState(const RenderState& renderState, ClearColor clearColor, float clearDepth, uint32_t clearStencil)
{
    if (activeRenderState) {
        LogWarning("setRenderState: already active render state!\n");
        endCurrentRenderPassIfAny();
    }
    activeRenderState = &renderState;

    const RenderTarget& renderTarget = renderState.renderTarget();
    const auto& targetInfo = m_backend.renderTargetInfo(renderTarget);

    std::vector<VkClearValue> clearValues {};
    {
        for (auto& [type, _] : renderTarget.sortedAttachments()) {
            VkClearValue value = {};
            if (type == RenderTarget::AttachmentType::Depth) {
                value.depthStencil = { clearDepth, clearStencil };
            } else {
                value.color = { { clearColor.r, clearColor.g, clearColor.b, clearColor.a } };
            }
            clearValues.push_back(value);
        }
    }

    VkRenderPassBeginInfo renderPassBeginInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };

    // (there is automatic image layout transitions for attached textures, so when we bind the
    //  render target here, make sure to also swap to the new layout in the cache variable)
    for (const auto& attachedTexture : targetInfo.attachedTextures) {
        m_backend.textureInfo(*attachedTexture).currentLayout = attachedTexture->hasDepthFormat()
            ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    // Explicitly transition the layouts of the sampled textures to an optimal layout (if it isn't already)
    {
        auto& stateInfo = m_backend.renderStateInfo(renderState);
        for (const Texture* texture : stateInfo.sampledTextures) {
            auto& texInfo = m_backend.textureInfo(*texture);
            if (texInfo.currentLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                m_backend.transitionImageLayout(texInfo.image, texInfo.format, texInfo.currentLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_commandBuffer);
            }
            texInfo.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
    }

    renderPassBeginInfo.renderPass = targetInfo.compatibleRenderPass;
    renderPassBeginInfo.framebuffer = targetInfo.framebuffer;

    auto& targetExtent = renderTarget.extent();
    renderPassBeginInfo.renderArea.offset = { 0, 0 };
    renderPassBeginInfo.renderArea.extent = { targetExtent.width(), targetExtent.height() };

    renderPassBeginInfo.clearValueCount = clearValues.size();
    renderPassBeginInfo.pClearValues = clearValues.data();

    // TODO: Handle subpasses properly!
    vkCmdBeginRenderPass(m_commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    auto& stateInfo = m_backend.renderStateInfo(renderState);
    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, stateInfo.pipeline);
}

void VulkanCommandList::bindSet(BindingSet& bindingSet, uint32_t index)
{
    if (!activeRenderState) {
        LogErrorAndExit("bindSet: no active render state to bind to!\n");
    }
    VkPipelineLayout pipelineLayout = m_backend.renderStateInfo(*activeRenderState).pipelineLayout;

    auto& bindInfo = m_backend.bindingSetInfo(bindingSet);

    vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, index, 1, &bindInfo.descriptorSet, 0, nullptr);
}

void VulkanCommandList::draw(Buffer& vertexBuffer, uint32_t vertexCount)
{
    VkBuffer vertBuffer = m_backend.bufferInfo(vertexBuffer).buffer;

    VkBuffer vertexBuffers[] = { vertBuffer };
    VkDeviceSize offsets[] = { 0 };

    vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdDraw(m_commandBuffer, vertexCount, 1, 0, 0);
}

void VulkanCommandList::drawIndexed(Buffer& vertexBuffer, Buffer& indexBuffer, uint32_t indexCount, uint32_t instanceIndex)
{
    VkBuffer vertBuffer = m_backend.bufferInfo(vertexBuffer).buffer;
    VkBuffer idxBuffer = m_backend.bufferInfo(indexBuffer).buffer;

    VkBuffer vertexBuffers[] = { vertBuffer };
    VkDeviceSize offsets[] = { 0 };

    vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(m_commandBuffer, idxBuffer, 0, VK_INDEX_TYPE_UINT16); // TODO: User should specify index type!
    vkCmdDrawIndexed(m_commandBuffer, indexCount, 1, 0, 0, instanceIndex);
}

void VulkanCommandList::debugBarrier()
{
    VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

    vkCmdPipelineBarrier(m_commandBuffer, sourceStage, destinationStage, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void VulkanCommandList::endNode(Badge<class VulkanBackend>)
{
    endCurrentRenderPassIfAny();
    debugBarrier(); // TODO: We probably don't need to do this..?
}

void VulkanCommandList::endCurrentRenderPassIfAny()
{
    if (activeRenderState) {
        vkCmdEndRenderPass(m_commandBuffer);
        activeRenderState = nullptr;
    }
}
