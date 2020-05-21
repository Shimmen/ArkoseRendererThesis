#include "VulkanCommandList.h"

#include "VulkanBackend.h"
#include <stb_image_write.h>

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

void VulkanCommandList::clearTexture(Texture& colorTexture, ClearColor color)
{
    ASSERT(!colorTexture.hasDepthFormat());

    const auto& texInfo = m_backend.textureInfo(colorTexture);

    std::optional<VkImageLayout> originalLayout;
    if (texInfo.currentLayout != VK_IMAGE_LAYOUT_GENERAL) {
        originalLayout = texInfo.currentLayout;
        m_backend.transitionImageLayout(texInfo.image, false, originalLayout.value(), VK_IMAGE_LAYOUT_GENERAL, &m_commandBuffer);
    }

    VkClearColorValue clearValue {};
    clearValue.float32[0] = color.r;
    clearValue.float32[1] = color.g;
    clearValue.float32[2] = color.b;
    clearValue.float32[3] = color.a;

    VkImageSubresourceRange range {};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    range.baseMipLevel = 0;
    range.levelCount = colorTexture.mipLevels();

    range.baseArrayLayer = 0;
    range.layerCount = 1;

    vkCmdClearColorImage(m_commandBuffer, texInfo.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &range);

    if (originalLayout.has_value()) {
        m_backend.transitionImageLayout(texInfo.image, false, VK_IMAGE_LAYOUT_GENERAL, originalLayout.value(), &m_commandBuffer);
    }
}

void VulkanCommandList::setRenderState(const RenderState& renderState, ClearColor clearColor, float clearDepth, uint32_t clearStencil)
{
    if (activeRenderState) {
        //LogWarning("setRenderState: already active render state!\n");
        endCurrentRenderPassIfAny();
    }
    activeRenderState = &renderState;

    activeRayTracingState = nullptr;
    activeComputeState = nullptr;

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
    for (const auto& [attachedTexture, implicitTransitionLayout] : targetInfo.attachedTextures) {
        m_backend.textureInfo(*attachedTexture).currentLayout = implicitTransitionLayout;
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

            // TODO: We probably want to support storage images here as well!
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
    activeComputeState = nullptr;

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

void VulkanCommandList::setComputeState(const ComputeState& computeState)
{
    if (activeRenderState) {
        LogWarning("setComputeState: active render state when starting compute state.\n");
        endCurrentRenderPassIfAny();
    }

    activeComputeState = &computeState;
    activeRayTracingState = nullptr;

    auto& computeStateInfo = m_backend.computeStateInfo(computeState);

    // Explicitly transition the layouts of the referenced textures to an optimal layout (if it isn't already)
    for (const Texture* texture : computeStateInfo.storageImages) {
        auto& texInfo = m_backend.textureInfo(*texture);
        if (texInfo.currentLayout != VK_IMAGE_LAYOUT_GENERAL) {
            m_backend.transitionImageLayout(texInfo.image, texture->hasDepthFormat(), texInfo.currentLayout, VK_IMAGE_LAYOUT_GENERAL, &m_commandBuffer);
        }
        texInfo.currentLayout = VK_IMAGE_LAYOUT_GENERAL;
    }

    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computeStateInfo.pipeline);
}

void VulkanCommandList::bindSet(BindingSet& bindingSet, uint32_t index)
{
    if (!activeRenderState && !activeRayTracingState && !activeComputeState) {
        LogErrorAndExit("bindSet: no active render or compute or ray tracing state to bind to!\n");
    }

    ASSERT(!(activeRenderState && activeRayTracingState && activeComputeState));

    VkPipelineLayout pipelineLayout;
    VkPipelineBindPoint bindPoint;

    if (activeRenderState) {
        pipelineLayout = m_backend.renderStateInfo(*activeRenderState).pipelineLayout;
        bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    }
    if (activeComputeState) {
        pipelineLayout = m_backend.computeStateInfo(*activeComputeState).pipelineLayout;
        bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
    }
    if (activeRayTracingState) {
        pipelineLayout = m_backend.rayTracingStateInfo(*activeRayTracingState).pipelineLayout;
        bindPoint = VK_PIPELINE_BIND_POINT_RAY_TRACING_NV;
    }

    auto& bindInfo = m_backend.bindingSetInfo(bindingSet);
    vkCmdBindDescriptorSets(m_commandBuffer, bindPoint, pipelineLayout, index, 1, &bindInfo.descriptorSet, 0, nullptr);
}

void VulkanCommandList::pushConstants(ShaderStage shaderStage, void* data, size_t size, size_t byteOffset)
{
    if (!activeRenderState && !activeRayTracingState && !activeComputeState) {
        LogErrorAndExit("pushConstants: no active render or compute or ray tracing state to bind to!\n");
    }

    ASSERT(!(activeRenderState && activeRayTracingState && activeComputeState));

    VkPipelineLayout pipelineLayout;
    if (activeRenderState) {
        pipelineLayout = m_backend.renderStateInfo(*activeRenderState).pipelineLayout;
    }
    if (activeComputeState) {
        pipelineLayout = m_backend.computeStateInfo(*activeComputeState).pipelineLayout;
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

    vkCmdPushConstants(m_commandBuffer, pipelineLayout, stageFlags, byteOffset, size, data);
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
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_NV | VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_NV;
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

    // Delete the old instance buffer & replace with the new one
    ASSERT(tlasInfo.associatedBuffers.size() == 1);
    auto& [prevInstanceBuf, prevInstanceAlloc] = tlasInfo.associatedBuffers[0];
    vmaDestroyBuffer(m_backend.m_memoryAllocator, prevInstanceBuf, prevInstanceAlloc);
    tlasInfo.associatedBuffers[0] = { instanceBuffer, instanceAllocation };
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
    VkBuffer sbtBuffer = rtStateInfo.sbtBuffer;

    uint32_t baseAlignment = m_backend.m_rtx->properties().shaderGroupBaseAlignment;

    uint32_t raygenOffset = 0; // we always start with raygen
    uint32_t raygenStride = baseAlignment; // since we have no data => TODO!
    size_t numRaygenShaders = 1; // for now, always just one

    uint32_t hitGroupOffset = raygenOffset + (numRaygenShaders * raygenStride);
    uint32_t hitGroupStride = baseAlignment; // since we have no data and a single shader for now => TODO! ALSO CONSIDER IF THIS SHOULD SIMPLY BE PASSED IN TO HERE?!
    size_t numHitGroups = activeRayTracingState->shaderBindingTable().hitGroups().size();

    uint32_t missOffset = hitGroupOffset + (numHitGroups * hitGroupStride);
    uint32_t missStride = baseAlignment; // since we have no data => TODO!

    m_backend.m_rtx->vkCmdTraceRaysNV(m_commandBuffer,
                                      sbtBuffer, raygenOffset,
                                      sbtBuffer, missOffset, missStride,
                                      sbtBuffer, hitGroupOffset, hitGroupStride,
                                      VK_NULL_HANDLE, 0, 0,
                                      extent.width(), extent.height(), 1);
}

void VulkanCommandList::dispatch(Extent3D globalSize, Extent3D localSize)
{
    uint32_t x = (globalSize.width() + localSize.width() - 1) / localSize.width();
    uint32_t y = (globalSize.height() + localSize.height() - 1) / localSize.height();
    uint32_t z = (globalSize.depth() + localSize.depth() - 1) / localSize.depth();
    dispatch(x, y, z);
}

void VulkanCommandList::dispatch(uint32_t x, uint32_t y, uint32_t z)
{
    if (!activeComputeState) {
        LogErrorAndExit("Trying to dispatch compute but there is no active compute state!\n");
    }
    vkCmdDispatch(m_commandBuffer, x, y, z);
}

void VulkanCommandList::waitEvent(uint8_t eventId, PipelineStage stage)
{
    VkEvent event = getEvent(eventId);
    VkPipelineStageFlags flags = stageFlags(stage);

    vkCmdWaitEvents(m_commandBuffer, 1, &event,
                    flags, flags, // TODO: Might be required that we have different stages here later!
                    0, nullptr,
                    0, nullptr,
                    0, nullptr);
}

void VulkanCommandList::resetEvent(uint8_t eventId, PipelineStage stage)
{
    VkEvent event = getEvent(eventId);
    vkCmdResetEvent(m_commandBuffer, event, stageFlags(stage));
}

void VulkanCommandList::signalEvent(uint8_t eventId, PipelineStage stage)
{
    VkEvent event = getEvent(eventId);
    vkCmdSetEvent(m_commandBuffer, event, stageFlags(stage));
}

void VulkanCommandList::saveTextureToFile(const Texture& texture, const std::string& filePath)
{
    const VkFormat targetFormat = VK_FORMAT_R8G8B8A8_UNORM;

    auto& srcTexInfo = m_backend.textureInfo(texture);
    VkImageLayout prevSrcLayout = srcTexInfo.currentLayout;
    VkImage srcImage = srcTexInfo.image;

    VkImageCreateInfo imageCreateInfo { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = targetFormat;
    imageCreateInfo.tiling = VK_IMAGE_TILING_LINEAR;
    imageCreateInfo.extent.width = texture.extent().width();
    imageCreateInfo.extent.height = texture.extent().height();
    imageCreateInfo.extent.depth = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocCreateInfo {};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;

    VkImage dstImage;
    VmaAllocation dstAllocation;
    VmaAllocationInfo dstAllocationInfo;
    if (vmaCreateImage(m_backend.m_memoryAllocator, &imageCreateInfo, &allocCreateInfo, &dstImage, &dstAllocation, &dstAllocationInfo) != VK_SUCCESS) {
        LogErrorAndExit("Failed to create temp image for screenshot\n");
    }

    bool success = m_backend.issueSingleTimeCommand([&](VkCommandBuffer cmdBuffer) {
        m_backend.transitionImageLayoutDEBUG(dstImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, cmdBuffer);
        m_backend.transitionImageLayoutDEBUG(srcImage, prevSrcLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, cmdBuffer);

        VkImageCopy imageCopyRegion {};
        imageCopyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageCopyRegion.srcSubresource.layerCount = 1;
        imageCopyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageCopyRegion.dstSubresource.layerCount = 1;
        imageCopyRegion.extent.width = texture.extent().width();
        imageCopyRegion.extent.height = texture.extent().height();
        imageCopyRegion.extent.depth = 1;

        vkCmdCopyImage(cmdBuffer,
                       srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &imageCopyRegion);

        // Transition destination image to general layout, which is the required layout for mapping the image memory
        m_backend.transitionImageLayoutDEBUG(dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT, cmdBuffer);
        m_backend.transitionImageLayoutDEBUG(srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, prevSrcLayout, VK_IMAGE_ASPECT_COLOR_BIT, cmdBuffer);
    });

    if (!success) {
        LogError("Failed to setup screenshot image & data...\n");
    }

    // Get layout of the image (including row pitch/stride)
    VkImageSubresource subResource;
    subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subResource.mipLevel = 0;
    subResource.arrayLayer = 0;
    VkSubresourceLayout subResourceLayout;
    vkGetImageSubresourceLayout(device(), dstImage, &subResource, &subResourceLayout);

    char* data;
    vkMapMemory(device(), dstAllocationInfo.deviceMemory, 0, VK_WHOLE_SIZE, 0, (void**)&data);
    data += subResourceLayout.offset;

    bool shouldSwizzleRedAndBlue = (srcTexInfo.format == VK_FORMAT_B8G8R8A8_SRGB) || (srcTexInfo.format == VK_FORMAT_B8G8R8A8_UNORM) || (srcTexInfo.format == VK_FORMAT_B8G8R8A8_SNORM);
    if (shouldSwizzleRedAndBlue) {
        int numPixels = texture.extent().width() * texture.extent().height();
        for (int i = 0; i < numPixels; ++i) {
            char tmp = data[4 * i + 0];
            data[4 * i + 0] = data[4 * i + 2];
            data[4 * i + 2] = tmp;
        }
    }

    if (!stbi_write_png(filePath.c_str(), texture.extent().width(), texture.extent().height(), 4, data, subResourceLayout.rowPitch)) {
        LogError("Failed to write screenshot to file...\n");
    }

    vkUnmapMemory(device(), dstAllocationInfo.deviceMemory);
    vmaDestroyImage(m_backend.m_memoryAllocator, dstImage, dstAllocation);
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

VkEvent VulkanCommandList::getEvent(uint8_t eventId)
{
    const auto& events = m_backend.m_events;

    if (eventId >= events.size()) {
        LogErrorAndExit("Event of id %u requested, which is >= than the number of created events (%u)\n", eventId, events.size());
    }
    return events[eventId];
}

VkPipelineStageFlags VulkanCommandList::stageFlags(PipelineStage stage) const
{
    switch (stage) {
    case PipelineStage::Host:
        return VK_PIPELINE_STAGE_HOST_BIT;
    case PipelineStage::RayTracing:
        return VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV;
    default:
        ASSERT(false);
    }
}
