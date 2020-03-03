#include "VulkanCommandList.h"

#include "VulkanBackend.h"

VulkanCommandList::VulkanCommandList(VulkanBackend& backend, VkCommandBuffer commandBuffer)
    : m_backend(backend)
    , m_commandBuffer(commandBuffer)
{
}

void VulkanCommandList::updateBufferImmediately(Buffer& buffer, void* data, size_t size)
{
    auto& bufInfo = m_backend.bufferInfo(buffer);

    switch (buffer.memoryHint()) {
    case Buffer::MemoryHint::TransferOptimal: {
        if (!m_backend.setBufferMemoryUsingMapping(bufInfo.allocation, data, size)) {
            LogError("updateBuffer(): could not update the buffer memory through mapping.\n");
        }
        break;
    }
    case Buffer::MemoryHint::GpuOptimal: {
        // TODO: We probably want to do it on our main command buffer and then add a barrier, right?
        //  The problem is that I get weird errors when I do that, and this works fine (i.e. using a one-off cmd buffer)
        if (!m_backend.setBufferDataUsingStagingBuffer(bufInfo.buffer, data, size)) {
            LogError("updateBuffer(): could not update the buffer memory through staging buffer.\n");
        }
        break;
    }
    default:
        LogError("updateBuffer(): can't update buffer with GpuOnly memory hint, ignoring\n");
    }
}

void VulkanCommandList::setRenderState(const RenderState& renderState, ClearColor clearColor, float clearDepth, uint32_t clearStencil)
{
    if (activeRenderState) {
        LogWarning("setRenderState: already active render state!\n");
        endCurrentRenderPassIfAny();
    }
    activeRenderState = &renderState;
    activeRayTracingState = nullptr;

    const RenderTarget& renderTarget = renderState.renderTarget();
    const auto& targetInfo = m_backend.renderTargetInfo(renderTarget);

    std::vector<VkClearValue> clearValues {};
    {
        for (auto& attachment : renderTarget.sortedAttachments()) {
            VkClearValue value = {};
            if (attachment.type == RenderTarget::AttachmentType::Depth) {
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
                m_backend.transitionImageLayout(texInfo.image, texture->hasDepthFormat(), texInfo.currentLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_commandBuffer);
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

void VulkanCommandList::setRayTracingState(const RayTracingState& rtState)
{
    if (!m_backend.m_rtx.has_value()) {
        LogErrorAndExit("Trying to set ray tracing state but there is no ray tracing support!\n");
    }

    if (activeRenderState) {
        LogWarning("setRayTracingState: active render state when starting ray tracing.\n");
        endCurrentRenderPassIfAny();
    }

    activeRayTracingState = &rtState;

    // Explicitly transition the layouts of the referenced textures to an optimal layout (if it isn't already)
    {
        auto& rtStateInfo = m_backend.rayTracingStateInfo(rtState);

        for (const Texture* texture : rtStateInfo.sampledTextures) {
            auto& texInfo = m_backend.textureInfo(*texture);
            if (texInfo.currentLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                m_backend.transitionImageLayout(texInfo.image, texture->hasDepthFormat(), texInfo.currentLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_commandBuffer);
            }
            texInfo.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        for (const Texture* texture : rtStateInfo.storageImages) {
            auto& texInfo = m_backend.textureInfo(*texture);
            if (texInfo.currentLayout != VK_IMAGE_LAYOUT_GENERAL) {
                m_backend.transitionImageLayout(texInfo.image, texture->hasDepthFormat(), texInfo.currentLayout, VK_IMAGE_LAYOUT_GENERAL, &m_commandBuffer);
            }
            texInfo.currentLayout = VK_IMAGE_LAYOUT_GENERAL;
        }
    }

    auto& rtStateInfo = m_backend.rayTracingStateInfo(rtState);
    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, rtStateInfo.pipeline);
}

void VulkanCommandList::bindSet(BindingSet& bindingSet, uint32_t index)
{
    if (!activeRenderState && !activeRayTracingState) {
        LogErrorAndExit("bindSet: no active render or ray tracing state to bind to!\n");
    }

    ASSERT(!(activeRenderState && activeRayTracingState));

    VkPipelineLayout pipelineLayout;
    VkPipelineBindPoint bindPoint;

    if (activeRenderState) {
        pipelineLayout = m_backend.renderStateInfo(*activeRenderState).pipelineLayout;
        bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    }
    if (activeRayTracingState) {
        pipelineLayout = m_backend.rayTracingStateInfo(*activeRayTracingState).pipelineLayout;
        bindPoint = VK_PIPELINE_BIND_POINT_RAY_TRACING_NV;
    }

    auto& bindInfo = m_backend.bindingSetInfo(bindingSet);
    vkCmdBindDescriptorSets(m_commandBuffer, bindPoint, pipelineLayout, index, 1, &bindInfo.descriptorSet, 0, nullptr);
}

void VulkanCommandList::pushConstants(ShaderStage shaderStage, void* data, size_t size)
{
    if (!activeRenderState && !activeRayTracingState) {
        LogErrorAndExit("pushConstants: no active render or ray tracing state to bind to!\n");
    }

    ASSERT(!(activeRenderState && activeRayTracingState));

    VkPipelineLayout pipelineLayout;
    if (activeRenderState) {
        pipelineLayout = m_backend.renderStateInfo(*activeRenderState).pipelineLayout;
    }
    if (activeRayTracingState) {
        pipelineLayout = m_backend.rayTracingStateInfo(*activeRayTracingState).pipelineLayout;
    }

    // TODO: This isn't the only occurance of this shady table. We probably want a function for doing this translation!
    VkShaderStageFlags stageFlags = 0u;
    if (shaderStage & ShaderStageVertex)
        stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
    if (shaderStage & ShaderStageFragment)
        stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (shaderStage & ShaderStageCompute)
        stageFlags |= VK_SHADER_STAGE_COMPUTE_BIT;
    if (shaderStage & ShaderStageRTRayGen)
        stageFlags |= VK_SHADER_STAGE_RAYGEN_BIT_NV;
    if (shaderStage & ShaderStageRTMiss)
        stageFlags |= VK_SHADER_STAGE_MISS_BIT_NV;
    if (shaderStage & ShaderStageRTClosestHit)
        stageFlags |= VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;

    vkCmdPushConstants(m_commandBuffer, pipelineLayout, stageFlags, 0, size, data);
}

void VulkanCommandList::draw(Buffer& vertexBuffer, uint32_t vertexCount)
{
    if (!activeRenderState) {
        LogErrorAndExit("draw: no active render state!\n");
    }

    VkBuffer vertBuffer = m_backend.bufferInfo(vertexBuffer).buffer;

    VkBuffer vertexBuffers[] = { vertBuffer };
    VkDeviceSize offsets[] = { 0 };

    vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdDraw(m_commandBuffer, vertexCount, 1, 0, 0);
}

void VulkanCommandList::drawIndexed(Buffer& vertexBuffer, Buffer& indexBuffer, uint32_t indexCount, IndexType indexType, uint32_t instanceIndex)
{
    if (!activeRenderState) {
        LogErrorAndExit("drawIndexed: no active render state!\n");
    }

    VkBuffer vertBuffer = m_backend.bufferInfo(vertexBuffer).buffer;
    VkBuffer idxBuffer = m_backend.bufferInfo(indexBuffer).buffer;

    VkBuffer vertexBuffers[] = { vertBuffer };
    VkDeviceSize offsets[] = { 0 };

    VkIndexType vkIndexType;
    switch (indexType) {
    case IndexType::UInt16:
        vkIndexType = VK_INDEX_TYPE_UINT16;
        break;
    case IndexType::UInt32:
        vkIndexType = VK_INDEX_TYPE_UINT32;
        break;
    }

    vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(m_commandBuffer, idxBuffer, 0, vkIndexType);
    vkCmdDrawIndexed(m_commandBuffer, indexCount, 1, 0, 0, instanceIndex);
}

void VulkanCommandList::rebuildTopLevelAcceratationStructure(TopLevelAS& tlas)
{
    if (!m_backend.m_rtx.has_value()) {
        LogErrorAndExit("Trying to rebuild a top level acceleration structure but there is no ray tracing support!\n");
    }

    auto& tlasInfo = m_backend.accelerationStructureInfo(tlas);

    // TODO: Maybe don't throw the allocation away (when building the first time), so we can reuse it here?
    //  However, it's a different size, though! So maybe not. Or if we use the max(build, rebuild) size?
    VmaAllocation scratchAllocation;
    VkBuffer scratchBuffer = m_backend.createScratchBufferForAccelerationStructure(tlasInfo.accelerationStructure, true, scratchAllocation);

    VmaAllocation instanceAllocation;
    VkBuffer instanceBuffer = m_backend.createRTXInstanceBuffer(tlas.instances(), instanceAllocation);

    VkAccelerationStructureInfoNV buildInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV };
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_NV;
    buildInfo.instanceCount = tlas.instanceCount();
    buildInfo.geometryCount = 0;
    buildInfo.pGeometries = nullptr;

    m_backend.m_rtx->vkCmdBuildAccelerationStructureNV(
        m_commandBuffer,
        &buildInfo,
        instanceBuffer, 0,
        VK_TRUE,
        tlasInfo.accelerationStructure,
        tlasInfo.accelerationStructure,
        scratchBuffer, 0);

    VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV;
    vkCmdPipelineBarrier(m_commandBuffer,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);

    vmaDestroyBuffer(m_backend.m_memoryAllocator, scratchBuffer, scratchAllocation);
    vmaDestroyBuffer(m_backend.m_memoryAllocator, instanceBuffer, instanceAllocation);
}

void VulkanCommandList::traceRays(Extent2D extent)
{
    if (!activeRayTracingState) {
        LogErrorAndExit("traceRays: no active ray tracing state!\n");
    }

    if (!m_backend.m_rtx.has_value()) {
        LogErrorAndExit("Trying to trace rays but there is no ray tracing support!\n");
    }

    auto& rtStateInfo = m_backend.rayTracingStateInfo(*activeRayTracingState);
    VkDeviceSize bindingStride = m_backend.m_rtx->properties().shaderGroupHandleSize;

    m_backend.m_rtx->vkCmdTraceRaysNV(m_commandBuffer,
                                      rtStateInfo.sbtBuffer, rtStateInfo.sbtRaygenIdx * bindingStride,
                                      rtStateInfo.sbtBuffer, rtStateInfo.sbtMissIdx * bindingStride, bindingStride,
                                      rtStateInfo.sbtBuffer, rtStateInfo.sbtClosestHitIdx * bindingStride, bindingStride,
                                      VK_NULL_HANDLE, 0, 0,
                                      extent.width(), extent.height(), 1);
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
