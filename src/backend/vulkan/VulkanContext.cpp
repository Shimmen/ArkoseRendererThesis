#include "VulkanContext.h"

#include "../mesh.h"
#include "camera_state.h"
#include "utility/fileio.h"
#include "utility/logging.h"
#include "utility/mathkit.h"
#include "utility/util.h"
#include <array>
#include <chrono>
#include <cstring>
#include <stb_image.h>

VulkanContext::VulkanContext(VkPhysicalDevice physicalDevice, VkDevice device)
    : m_physicalDevice(physicalDevice)
    , m_device(device)
{

    uint32_t graphicsQueueFamily = 0; // TODO TODO TODO
    vkGetDeviceQueue(m_device, graphicsQueueFamily, 0, &m_graphicsQueue);

    VkCommandPoolCreateInfo poolCreateInfo = {};
    poolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCreateInfo.queueFamilyIndex = graphicsQueueFamily;
    poolCreateInfo.flags = 0u;
    if (vkCreateCommandPool(device, &poolCreateInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend::VulkanContext(): could not create command pool for the graphics queue, exiting.\n");
    }

    VkCommandPoolCreateInfo transientPoolCreateInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    transientPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    transientPoolCreateInfo.queueFamilyIndex = graphicsQueueFamily;
    if (vkCreateCommandPool(m_device, &transientPoolCreateInfo, nullptr, &m_transientCommandPool) != VK_SUCCESS) {
        LogErrorAndExit("VulkanContext::VulkanContext(): could not create transient command pool, exiting.\n");
    }
}

VulkanContext::~VulkanContext()
{
    for (const auto& buffer : m_managedBuffers) {
        vkDestroyBuffer(m_device, buffer.buffer, nullptr);
        vkFreeMemory(m_device, buffer.memory, nullptr);
    }

    for (const auto& image : m_managedImages) {
        vkDestroySampler(m_device, image.sampler, nullptr);
        vkDestroyImageView(m_device, image.view, nullptr);
        vkDestroyImage(m_device, image.image, nullptr);
        vkFreeMemory(m_device, image.memory, nullptr);
    }

    vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    vkDestroyCommandPool(m_device, m_transientCommandPool, nullptr);
}

bool VulkanContext::issueSingleTimeCommand(const std::function<void(VkCommandBuffer)>& callback) const
{
    VkCommandBufferAllocateInfo commandBufferAllocInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    commandBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocInfo.commandPool = m_transientCommandPool;
    commandBufferAllocInfo.commandBufferCount = 1;

    VkCommandBuffer oneTimeCommandBuffer;
    vkAllocateCommandBuffers(m_device, &commandBufferAllocInfo, &oneTimeCommandBuffer);
    AT_SCOPE_EXIT([&] {
        vkFreeCommandBuffers(m_device, m_transientCommandPool, 1, &oneTimeCommandBuffer);
    });

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(oneTimeCommandBuffer, &beginInfo) != VK_SUCCESS) {
        LogError("VulkanContext::issueSingleTimeCommand(): could not begin the command buffer.\n");
        return false;
    }

    callback(oneTimeCommandBuffer);

    if (vkEndCommandBuffer(oneTimeCommandBuffer) != VK_SUCCESS) {
        LogError("VulkanContext::issueSingleTimeCommand(): could not end the command buffer.\n");
        return false;
    }

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &oneTimeCommandBuffer;

    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        LogError("VulkanContext::issueSingleTimeCommand(): could not submit the single-time command buffer.\n");
        return false;
    }
    if (vkQueueWaitIdle(m_graphicsQueue) != VK_SUCCESS) {
        LogError("VulkanContext::issueSingleTimeCommand(): error while waiting for the graphics queue to idle.\n");
        return false;
    }

    return true;
}

VkBuffer VulkanContext::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryProperties, VkDeviceMemory& memory)
{
    VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferCreateInfo.size = size;
    bufferCreateInfo.usage = usage;

    VkBuffer buffer;
    if (vkCreateBuffer(m_device, &bufferCreateInfo, nullptr, &buffer) != VK_SUCCESS) {
        LogError("VulkanContext::createBuffer(): could not create buffer of size %u.\n", size);
        memory = {};
        return {};
    }

    VkMemoryRequirements memoryRequirements;
    vkGetBufferMemoryRequirements(m_device, buffer, &memoryRequirements);

    VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.memoryTypeIndex = findAppropriateMemory(memoryRequirements.memoryTypeBits, memoryProperties);
    allocInfo.allocationSize = memoryRequirements.size;

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        LogError("VulkanContext::createBuffer(): could not allocate the required memory of size %u.\n", size);
        memory = {};
        return {};
    }

    if (vkBindBufferMemory(m_device, buffer, memory, 0) != VK_SUCCESS) {
        LogError("VulkanContext::createBuffer(): could not bind the allocated memory to the buffer.\n");
        return {};
    }

    return buffer;
}

bool VulkanContext::copyBuffer(VkBuffer source, VkBuffer destination, VkDeviceSize size) const
{
    VkBufferCopy bufferCopyRegion = {};
    bufferCopyRegion.size = size;
    bufferCopyRegion.srcOffset = 0;
    bufferCopyRegion.dstOffset = 0;

    bool success = issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
        vkCmdCopyBuffer(commandBuffer, source, destination, 1, &bufferCopyRegion);
    });

    if (!success) {
        LogError("VulkanContext::copyBuffer(): error copying buffer, refer to issueSingleTimeCommand errors for more information.\n");
        return false;
    }

    return true;
}

bool VulkanContext::setBufferMemoryDirectly(VkDeviceMemory memory, const void* data, VkDeviceSize size, VkDeviceSize offset)
{
    void* sharedMemory;
    if (vkMapMemory(m_device, memory, offset, size, 0u, &sharedMemory) != VK_SUCCESS) {
        LogError("VulkanContext::setBufferMemoryDirectly(): could not map the memory for loading data.\n");
        return false;
    }
    std::memcpy(sharedMemory, data, size);
    vkUnmapMemory(m_device, memory);

    return true;
}

bool VulkanContext::setBufferDataUsingStagingBuffer(VkBuffer buffer, const void* data, VkDeviceSize size, VkDeviceSize offset)
{
    VkDeviceMemory stagingMemory;
    VkBuffer stagingBuffer = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingMemory);

    AT_SCOPE_EXIT([&] {
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingMemory, nullptr);
    });

    if (!setBufferMemoryDirectly(stagingMemory, data, size)) {
        LogError("VulkanContext::setBufferDataUsingStagingBuffer(): could not set staging data.\n");
        return false;
    }

    if (!copyBuffer(stagingBuffer, buffer, size)) {
        LogError("VulkanContext::setBufferDataUsingStagingBuffer(): could not copy from staging buffer to buffer.\n");
        return false;
    }

    return true;
}

VkBuffer VulkanContext::createDeviceLocalBuffer(VkDeviceSize size, const void* data, VkBufferUsageFlags usage)
{
    // Make sure that we can transfer to this buffer
    usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VkDeviceMemory memory;
    VkBuffer buffer = createBuffer(size, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memory);

    if (!setBufferDataUsingStagingBuffer(buffer, data, size)) {
        LogError("VulkanContext::createDeviceLocalBuffer(): could not set data through a staging buffer.\n");
    }

    // FIXME: This is only temporary! Later we should keep some shared memory which with offset buffers etc..
    m_managedBuffers.push_back({ buffer, memory });

    return buffer;
}

VkImage VulkanContext::createImage2D(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkMemoryPropertyFlags memoryProperties, VkDeviceMemory& memory, VkImageTiling tiling)
{
    VkImageCreateInfo imageCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.extent = { .width = width, .height = height, .depth = 1 };
    imageCreateInfo.mipLevels = 1; // FIXME?
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.usage = usage;
    imageCreateInfo.format = format;
    imageCreateInfo.tiling = tiling;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkImage image;
    if (vkCreateImage(m_device, &imageCreateInfo, nullptr, &image) != VK_SUCCESS) {
        LogError("VulkanContext::createImage2D(): could not create image.\n");
        memory = {};
        return {};
    }

    VkMemoryRequirements memoryRequirements;
    vkGetImageMemoryRequirements(m_device, image, &memoryRequirements);

    VkMemoryAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocateInfo.memoryTypeIndex = findAppropriateMemory(memoryRequirements.memoryTypeBits, memoryProperties);
    allocateInfo.allocationSize = memoryRequirements.size;

    if (vkAllocateMemory(m_device, &allocateInfo, nullptr, &memory) != VK_SUCCESS) {
        LogError("VulkanContext::createImage2D(): could not allocate memory for image.\n");
        memory = {};
        return {};
    }

    if (vkBindImageMemory(m_device, image, memory, 0) != VK_SUCCESS) {
        LogError("VulkanContext::createImage2D(): could not bind the allocated memory to the image.\n");
        return {};
    }

    return image;
}

VkImageView VulkanContext::createImageView2D(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) const
{
    VkImageViewCreateInfo viewCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };

    viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewCreateInfo.subresourceRange.aspectMask = aspectFlags;
    viewCreateInfo.image = image;
    viewCreateInfo.format = format;

    viewCreateInfo.components = {
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY
    };

    viewCreateInfo.subresourceRange.baseMipLevel = 0;
    viewCreateInfo.subresourceRange.levelCount = 1;
    viewCreateInfo.subresourceRange.baseArrayLayer = 0;
    viewCreateInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(m_device, &viewCreateInfo, nullptr, &imageView) != VK_SUCCESS) {
        LogError("VulkanContext::createImageView2D(): could not create the image view.\n");
    }

    return imageView;
}

bool VulkanContext::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) const
{
    VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    imageBarrier.oldLayout = oldLayout;
    imageBarrier.newLayout = newLayout;
    imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    imageBarrier.image = image;
    imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageBarrier.subresourceRange.baseMipLevel = 0;
    imageBarrier.subresourceRange.levelCount = 1;
    imageBarrier.subresourceRange.baseArrayLayer = 0;
    imageBarrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        imageBarrier.srcAccessMask = 0;

        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    } else {
        LogErrorAndExit("VulkanContext::transitionImageLayout(): old & new layout combination unsupported by application, exiting.\n");
    }

    bool success = issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
        vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0,
            0, nullptr,
            0, nullptr,
            1, &imageBarrier);
    });

    if (!success) {
        LogError("VulkanContext::transitionImageLayout(): error transitioning layout, refer to issueSingleTimeCommand errors for more information.\n");
        return false;
    }

    return true;
}

bool VulkanContext::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const
{
    VkBufferImageCopy region = {};
    region.bufferOffset = 0;

    // (zeros here indicate tightly packed data)
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageOffset = VkOffset3D { 0, 0, 0 };
    region.imageExtent = VkExtent3D { width, height, 1 };

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    bool success = issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
        // NOTE: This assumes that the image we are copying to has the VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL layout!
        vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    });

    if (!success) {
        LogError("VulkanContext::copyBufferToImage(): error copying buffer to image, refer to issueSingleTimeCommand errors for more information.\n");
        return false;
    }

    return true;
}

ManagedImage VulkanContext::createImageViewFromImagePath(const std::string& imagePath)
{
    if (!fileio::isFileReadable(imagePath)) {
        LogError("VulkanContext::createImageFromImage(): there is no file that can be read at path '%s'.\n", imagePath.c_str());
        return {};
    }

    ASSERT(!stbi_is_hdr(imagePath.c_str()));

    int width, height, numChannels; // FIXME: Check the number of channels instead of forcing RGBA
    stbi_uc* pixels = stbi_load(imagePath.c_str(), &width, &height, &numChannels, STBI_rgb_alpha);
    if (!pixels) {
        LogError("VulkanContext::createImageFromImage(): stb_image could not read the contents of '%s'.\n", imagePath.c_str());
        stbi_image_free(pixels);
        return {};
    }

    VkDeviceSize imageSize = width * height * numChannels * sizeof(stbi_uc);
    VkFormat imageFormat = VK_FORMAT_B8G8R8A8_UNORM; // TODO: Use sRGB images for this type of stuff!

    VkDeviceMemory stagingMemory;
    VkBuffer stagingBuffer = createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingMemory);
    if (!setBufferMemoryDirectly(stagingMemory, pixels, imageSize)) {
        LogError("VulkanContext::createImageFromImagePath(): could not set the staging buffer memory.\n");
    }
    AT_SCOPE_EXIT([&]() {
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingMemory, nullptr);
        stbi_image_free(pixels);
    });

    VkDeviceMemory imageMemory;
    VkImage image = createImage2D(width, height, imageFormat, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, imageMemory);

    if (!transitionImageLayout(image, imageFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)) {
        LogError("VulkanContext::createImageFromImagePath(): could not transition the image to transfer layout.\n");
    }
    if (!copyBufferToImage(stagingBuffer, image, width, height)) {
        LogError("VulkanContext::createImageFromImagePath(): could not copy the staging buffer to the image.\n");
    }
    if (!transitionImageLayout(image, imageFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
        LogError("VulkanContext::createImageFromImagePath(): could not transition the image to shader-read-only layout.\n");
    }

    VkImageView imageView = createImageView2D(image, imageFormat, VK_IMAGE_ASPECT_COLOR_BIT);

    VkSamplerCreateInfo samplerCreateInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;
    samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
    samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
    samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerCreateInfo.anisotropyEnable = VK_TRUE;
    samplerCreateInfo.maxAnisotropy = 16.0f;
    samplerCreateInfo.compareEnable = VK_FALSE;
    samplerCreateInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerCreateInfo.mipLodBias = 0.0f;
    samplerCreateInfo.minLod = 0.0f;
    samplerCreateInfo.maxLod = 0.0f;

    VkSampler sampler;
    if (vkCreateSampler(m_device, &samplerCreateInfo, nullptr, &sampler) != VK_SUCCESS) {
        LogError("VulkanContext::createImageFromImagePath(): could not create the sampler for the image.\n");
    }

    ManagedImage managedImage = { sampler, imageView, image, imageMemory };

    // FIXME: This is only temporary! Later we should keep some shared memory which with offset buffers etc..
    m_managedImages.push_back(managedImage);

    return managedImage;
}

void VulkanContext::createTheDrawingStuff(VkFormat finalTargetFormat, VkExtent2D finalTargetExtent, const std::vector<VkImageView>& swapchainImageViews, VkImageView depthImageView, VkFormat depthFormat)
{
    m_exAspectRatio = float(finalTargetExtent.width) / float(finalTargetExtent.height);

    VkDescriptorSetLayoutBinding cameraStateUboLayoutBinding = {};
    cameraStateUboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    cameraStateUboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    cameraStateUboLayoutBinding.binding = 0;
    cameraStateUboLayoutBinding.descriptorCount = 1;
    cameraStateUboLayoutBinding.pImmutableSamplers = nullptr;

    ManagedImage testImage = createImageViewFromImagePath("assets/test-pattern.png");
    //m_exImageView = testImage.view;
    //m_exImageSampler = testImage.sampler;
    VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;

    std::array<VkDescriptorSetLayoutBinding, 2> allBindings = { cameraStateUboLayoutBinding, samplerLayoutBinding };

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    descriptorSetLayoutCreateInfo.bindingCount = allBindings.size();
    descriptorSetLayoutCreateInfo.pBindings = allBindings.data();
    ASSERT(vkCreateDescriptorSetLayout(m_device, &descriptorSetLayoutCreateInfo, nullptr, &m_exDescriptorSetLayout) == VK_SUCCESS);

    VkVertexInputBindingDescription bindingDescription = {};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(mesh::common::Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions {};

    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(mesh::common::Vertex, position);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(mesh::common::Vertex, color);

    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(mesh::common::Vertex, texCoord);

    {
        VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
        pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutCreateInfo.setLayoutCount = 1;
        pipelineLayoutCreateInfo.pSetLayouts = &m_exDescriptorSetLayout;
        pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
        ASSERT(vkCreatePipelineLayout(m_device, &pipelineLayoutCreateInfo, nullptr, &m_exPipelineLayout) == VK_SUCCESS);

        // Setup fixed functions

        VkPipelineVertexInputStateCreateInfo vertInputState = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vertInputState.vertexBindingDescriptionCount = 1;
        vertInputState.pVertexBindingDescriptions = &bindingDescription;
        vertInputState.vertexAttributeDescriptionCount = attributeDescriptions.size();
        vertInputState.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
        inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssemblyState.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(finalTargetExtent.width);
        viewport.height = static_cast<float>(finalTargetExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor = {};
        scissor.offset = { 0, 0 };
        scissor.extent = finalTargetExtent;

        VkPipelineViewportStateCreateInfo viewportState = {};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer = {};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling = {};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending = {};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        // TODO: Consider what we want here!
        VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_LINE_WIDTH
        };
        VkPipelineDynamicStateCreateInfo dynamicState = {};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;

        VkAttachmentDescription colorAttachment = {};
        colorAttachment.format = finalTargetFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription depthAttachment = {};
        depthAttachment.format = depthFormat;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorAttachmentRef = {};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthAttachmentRef = {};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        subpass.pDepthStencilAttachment = &depthAttachmentRef;

        std::array<VkAttachmentDescription, 2> allAttachments = { colorAttachment, depthAttachment };

        VkRenderPassCreateInfo renderPassCreateInfo = {};
        renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassCreateInfo.attachmentCount = allAttachments.size();
        renderPassCreateInfo.pAttachments = allAttachments.data();
        renderPassCreateInfo.subpassCount = 1;
        renderPassCreateInfo.pSubpasses = &subpass;

        // setup subpass dependency to make sure we have the right stuff before drawing to a swapchain image
        // see https://vulkan-tutorial.com/en/Drawing_a_triangle/Drawing/Rendering_and_presentation for info...
        VkSubpassDependency subpassDependency = {};
        subpassDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        subpassDependency.dstSubpass = 0; // i.e. the first and only subpass we have here
        subpassDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDependency.srcAccessMask = 0;
        subpassDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        renderPassCreateInfo.dependencyCount = 1;
        renderPassCreateInfo.pDependencies = &subpassDependency;

        ASSERT(vkCreateRenderPass(m_device, &renderPassCreateInfo, nullptr, &m_exRenderPass) == VK_SUCCESS);

        VkShaderModule vertShaderModule;
        VkPipelineShaderStageCreateInfo vertStageCreateInfo = {};
        {
            auto optionalData = fileio::loadEntireFileAsByteBuffer("shaders/example.vert.spv");
            ASSERT(optionalData.has_value());
            const auto& binaryData = optionalData.value();

            VkShaderModuleCreateInfo moduleCreateInfo = {};
            moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            moduleCreateInfo.codeSize = binaryData.size();
            moduleCreateInfo.pCode = reinterpret_cast<const uint32_t*>(binaryData.data());
            ASSERT(vkCreateShaderModule(m_device, &moduleCreateInfo, nullptr, &vertShaderModule) == VK_SUCCESS);

            vertStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            vertStageCreateInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
            vertStageCreateInfo.module = vertShaderModule;
            vertStageCreateInfo.pName = "main";
        }

        VkShaderModule fragShaderModule;
        VkPipelineShaderStageCreateInfo fragStageCreateInfo = {};
        {
            auto optionalData = fileio::loadEntireFileAsByteBuffer("shaders/example.frag.spv");
            ASSERT(optionalData.has_value());
            const auto& binaryData = optionalData.value();

            VkShaderModuleCreateInfo moduleCreateInfo = {};
            moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            moduleCreateInfo.codeSize = binaryData.size();
            moduleCreateInfo.pCode = reinterpret_cast<const uint32_t*>(binaryData.data());
            ASSERT(vkCreateShaderModule(m_device, &moduleCreateInfo, nullptr, &fragShaderModule) == VK_SUCCESS);

            fragStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            fragStageCreateInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            fragStageCreateInfo.module = fragShaderModule;
            fragStageCreateInfo.pName = "main";
        }

        VkPipelineDepthStencilStateCreateInfo depthStencilState = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        depthStencilState.depthTestEnable = VK_TRUE;
        depthStencilState.depthWriteEnable = VK_TRUE;
        depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencilState.depthBoundsTestEnable = VK_FALSE;
        depthStencilState.minDepthBounds = 0.0f;
        depthStencilState.maxDepthBounds = 1.0f;
        depthStencilState.stencilTestEnable = VK_FALSE;
        depthStencilState.front = {};
        depthStencilState.back = {};

        VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        // stages
        VkPipelineShaderStageCreateInfo shaderStages[] = { vertStageCreateInfo, fragStageCreateInfo };
        pipelineCreateInfo.stageCount = 2;
        pipelineCreateInfo.pStages = shaderStages;
        // fixed function stuff
        pipelineCreateInfo.pVertexInputState = &vertInputState;
        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pRasterizationState = &rasterizer;
        pipelineCreateInfo.pMultisampleState = &multisampling;
        pipelineCreateInfo.pDepthStencilState = &depthStencilState;
        pipelineCreateInfo.pColorBlendState = &colorBlending;
        pipelineCreateInfo.pDynamicState = nullptr; //&dynamicState;
        // pipeline layout
        pipelineCreateInfo.layout = m_exPipelineLayout;
        // render pass stuff
        pipelineCreateInfo.renderPass = m_exRenderPass;
        pipelineCreateInfo.subpass = 0;
        // extra stuff (optional for this)
        pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
        pipelineCreateInfo.basePipelineIndex = -1;

        ASSERT(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_exGraphicsPipeline) == VK_SUCCESS);

        vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
        vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
    }

    size_t numSwapchainImages = swapchainImageViews.size();

    m_targetFramebuffers.resize(numSwapchainImages);
    for (size_t it = 0; it < numSwapchainImages; ++it) {
        std::array<VkImageView, 2> attachments = {
            swapchainImageViews[it],
            depthImageView
        };

        VkFramebufferCreateInfo framebufferCreateInfo = {};
        framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferCreateInfo.renderPass = m_exRenderPass;
        framebufferCreateInfo.attachmentCount = attachments.size();
        framebufferCreateInfo.pAttachments = attachments.data();
        framebufferCreateInfo.width = finalTargetExtent.width;
        framebufferCreateInfo.height = finalTargetExtent.height;
        framebufferCreateInfo.layers = 1;

        ASSERT(vkCreateFramebuffer(m_device, &framebufferCreateInfo, nullptr, &m_targetFramebuffers[it]) == VK_SUCCESS);
    }

    m_exCameraStateBuffers.resize(numSwapchainImages);
    m_exCameraStateBufferMemories.resize(numSwapchainImages);
    for (size_t it = 0; it < numSwapchainImages; ++it) {

        VkDeviceMemory memory;
        VkBuffer buffer = createBuffer(sizeof(CameraState), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memory);

        m_exCameraStateBuffers[it] = buffer;
        m_exCameraStateBufferMemories[it] = memory;

        // TODO: Don't manage the memory like this!
        m_managedBuffers.push_back({ buffer, memory });
    }

    {
        std::array<VkDescriptorPoolSize, 2> descriptorPoolSizes {};
        descriptorPoolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorPoolSizes[0].descriptorCount = numSwapchainImages;
        descriptorPoolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorPoolSizes[1].descriptorCount = numSwapchainImages;

        VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        descriptorPoolCreateInfo.poolSizeCount = descriptorPoolSizes.size();
        descriptorPoolCreateInfo.pPoolSizes = descriptorPoolSizes.data();
        descriptorPoolCreateInfo.maxSets = numSwapchainImages;

        ASSERT(vkCreateDescriptorPool(m_device, &descriptorPoolCreateInfo, nullptr, &m_exDescriptorPool) == VK_SUCCESS);

        std::vector<VkDescriptorSetLayout> descriptorSetLayouts { numSwapchainImages, m_exDescriptorSetLayout };
        VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        descriptorSetAllocateInfo.descriptorPool = m_exDescriptorPool;
        descriptorSetAllocateInfo.descriptorSetCount = numSwapchainImages;
        descriptorSetAllocateInfo.pSetLayouts = descriptorSetLayouts.data();

        m_exDescriptorSets.resize(numSwapchainImages);
        ASSERT(vkAllocateDescriptorSets(m_device, &descriptorSetAllocateInfo, m_exDescriptorSets.data()) == VK_SUCCESS);

        for (size_t i = 0; i < numSwapchainImages; ++i) {
            VkDescriptorBufferInfo descriptorBufferInfo = {};
            descriptorBufferInfo.buffer = m_exCameraStateBuffers[i];
            descriptorBufferInfo.range = VK_WHOLE_SIZE; //sizeof(CameraState);
            descriptorBufferInfo.offset = 0;

            VkDescriptorImageInfo descriptorImageInfo = {};
            descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            descriptorImageInfo.imageView = testImage.view;
            descriptorImageInfo.sampler = testImage.sampler;

            VkWriteDescriptorSet uniformBufferWriteDescriptorSet = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            uniformBufferWriteDescriptorSet.dstSet = m_exDescriptorSets[i];
            uniformBufferWriteDescriptorSet.dstBinding = 0;
            uniformBufferWriteDescriptorSet.dstArrayElement = 0;
            uniformBufferWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            uniformBufferWriteDescriptorSet.descriptorCount = 1;
            uniformBufferWriteDescriptorSet.pBufferInfo = &descriptorBufferInfo;
            uniformBufferWriteDescriptorSet.pImageInfo = nullptr;
            uniformBufferWriteDescriptorSet.pTexelBufferView = nullptr;

            VkWriteDescriptorSet imageSamplerWriteDescriptorSet = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            imageSamplerWriteDescriptorSet.dstSet = m_exDescriptorSets[i];
            imageSamplerWriteDescriptorSet.dstBinding = 1;
            imageSamplerWriteDescriptorSet.dstArrayElement = 0;
            imageSamplerWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            imageSamplerWriteDescriptorSet.descriptorCount = 1;
            imageSamplerWriteDescriptorSet.pImageInfo = &descriptorImageInfo;
            imageSamplerWriteDescriptorSet.pBufferInfo = nullptr;
            imageSamplerWriteDescriptorSet.pTexelBufferView = nullptr;

            std::array<VkWriteDescriptorSet, 2> allWriteDescriptorSets = { uniformBufferWriteDescriptorSet, imageSamplerWriteDescriptorSet };
            vkUpdateDescriptorSets(m_device, allWriteDescriptorSets.size(), allWriteDescriptorSets.data(), 0, nullptr);
        }
    }

    // Create command buffers (one per swapchain target image)
    m_commandBuffers.resize(numSwapchainImages);
    {
        VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
        commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandBufferAllocateInfo.commandPool = m_commandPool;
        commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; // Can be submitted to a queue for execution, but cannot be called from other command buffers.
        commandBufferAllocateInfo.commandBufferCount = m_commandBuffers.size();

        ASSERT(vkAllocateCommandBuffers(m_device, &commandBufferAllocateInfo, m_commandBuffers.data()) == VK_SUCCESS);
    }

    // TODO: The command buffer recording also needs to be redone for the new command buffers,
    // TODO  but I think it seems like a separate step though... Or is it..?

    // TODO: This is the command buffer recording for the example triangle drawing stuff
    std::vector<mesh::common::Vertex> vertices = {
        { vec3(-0.5, -0.5, 0), vec3(1, 0, 0), vec2(1, 0) },
        { vec3(0.5, -0.5, 0), vec3(0, 1, 0), vec2(0, 0) },
        { vec3(0.5, 0.5, 0), vec3(0, 0, 1), vec2(0, 1) },
        { vec3(-0.5, 0.5, 0), vec3(1, 1, 1), vec2(1, 1) },

        { { -0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f } },
        { { 0.5f, -0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } },
        { { 0.5f, 0.5f, -0.5f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
        { { -0.5f, 0.5f, -0.5f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } }
    };
    std::vector<uint16_t> indices = {
        0, 1, 2,
        2, 3, 0,

        4, 5, 6,
        6, 7, 4
    };
    VkBuffer vertexBuffer = createDeviceLocalBuffer(vertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    VkBuffer indexBuffer = createDeviceLocalBuffer(indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    for (size_t it = 0; it < m_commandBuffers.size(); ++it) {

        VkCommandBufferBeginInfo commandBufferBeginInfo = {};
        commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandBufferBeginInfo.flags = 0u;
        commandBufferBeginInfo.pInheritanceInfo = nullptr;

        ASSERT(vkBeginCommandBuffer(m_commandBuffers[it], &commandBufferBeginInfo) == VK_SUCCESS);
        {
            std::array<VkClearValue, 2> clearValues {};
            clearValues[0].color = { 1.0f, 0.0f, 1.0f, 1.0f };
            clearValues[1].depthStencil = { 1.0f, 0 };

            VkRenderPassBeginInfo renderPassBeginInfo = {};
            renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassBeginInfo.renderPass = m_exRenderPass;
            renderPassBeginInfo.framebuffer = m_targetFramebuffers[it];
            renderPassBeginInfo.renderArea.offset = { 0, 0 };
            renderPassBeginInfo.renderArea.extent = finalTargetExtent;
            renderPassBeginInfo.clearValueCount = clearValues.size();
            renderPassBeginInfo.pClearValues = clearValues.data();

            vkCmdBeginRenderPass(m_commandBuffers[it], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
            {
                vkCmdBindPipeline(m_commandBuffers[it], VK_PIPELINE_BIND_POINT_GRAPHICS, m_exGraphicsPipeline);

                vkCmdBindDescriptorSets(m_commandBuffers[it], VK_PIPELINE_BIND_POINT_GRAPHICS, m_exPipelineLayout, 0, 1, &m_exDescriptorSets[it], 0, nullptr);

                VkBuffer vertexBuffers[] = { vertexBuffer };
                VkDeviceSize offsets[] = { 0 };
                vkCmdBindVertexBuffers(m_commandBuffers[it], 0, 1, vertexBuffers, offsets);
                vkCmdBindIndexBuffer(m_commandBuffers[it], indexBuffer, 0, VK_INDEX_TYPE_UINT16);

                vkCmdDrawIndexed(m_commandBuffers[it], indices.size(), 1, 0, 0, 0);
            }
            vkCmdEndRenderPass(m_commandBuffers[it]);
        }
        ASSERT(vkEndCommandBuffer(m_commandBuffers[it]) == VK_SUCCESS);
    }
}

void VulkanContext::destroyTheDrawingStuff()
{
    size_t numSwapchainImages = m_targetFramebuffers.size();
    for (size_t i = 0; i < numSwapchainImages; ++i) {
        vkDestroyFramebuffer(m_device, m_targetFramebuffers[i], nullptr);
    }

    vkFreeCommandBuffers(m_device, m_commandPool, m_commandBuffers.size(), m_commandBuffers.data());

    vkDestroyDescriptorPool(m_device, m_exDescriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_exDescriptorSetLayout, nullptr);
    vkDestroyPipeline(m_device, m_exGraphicsPipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_exPipelineLayout, nullptr);
    vkDestroyRenderPass(m_device, m_exRenderPass, nullptr);
}

void VulkanContext::timestepForTheDrawingStuff(uint32_t index)
{
    // FIXME: Use the time stuff provided by GLFW
    static auto startTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    // FIXME: We need access to the aspect ratio here!!

    // Update the uniform buffer(s)
    CameraState cameraState = {};
    cameraState.world_from_local = mathkit::axisAngle({ 0, 1, 0 }, time * 3.1415f / 2.0f);
    cameraState.view_from_world = mathkit::lookAt({ 0, 1, -2 }, { 0, 0, 0 });
    cameraState.projection_from_view = mathkit::infinitePerspective(mathkit::radians(45), m_exAspectRatio, 0.1f);

    cameraState.view_from_local = cameraState.view_from_world * cameraState.world_from_local;
    cameraState.projection_from_local = cameraState.projection_from_view * cameraState.view_from_local;

    if (!setBufferMemoryDirectly(m_exCameraStateBufferMemories[index], &cameraState, sizeof(CameraState))) {
        LogError("VulkanContext::timestepForTheDrawingStuff(): could not update the uniform buffer.\n");
    }
}

void VulkanContext::submitQueue(uint32_t imageIndex, VkSemaphore* waitFor, VkSemaphore* signal, VkFence* inFlight)
{
    // FIXME: This is the "active" part for a later "render pass" abstraction
    timestepForTheDrawingStuff(imageIndex);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitFor;
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffers[imageIndex];

    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signal;

    if (vkResetFences(m_device, 1, inFlight) != VK_SUCCESS) {
        LogError("VulkanContext::submitQueue(): error resetting in-flight frame fence (index %u).\n", imageIndex);
    }

    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, *inFlight) != VK_SUCCESS) {
        LogError("VulkanContext::submitQueue(): could not submit the graphics queue (index %u).\n", imageIndex);
    }
}

uint32_t VulkanContext::findAppropriateMemory(uint32_t typeBits, VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memoryProperties);

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++) {
        // Is type i at all supported, given the typeBits?
        if (!(typeBits & (1u << i))) {
            continue;
        }

        if ((memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    LogErrorAndExit("VulkanContext::findAppropriateMemory(): could not find any appropriate memory, exiting.\n");
}
