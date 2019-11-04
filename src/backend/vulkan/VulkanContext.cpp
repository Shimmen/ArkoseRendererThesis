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

    vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    vkDestroyCommandPool(m_device, m_transientCommandPool, nullptr);
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
    VkCommandBufferAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandPool = m_transientCommandPool;
    allocateInfo.commandBufferCount = 1;

    VkCommandBuffer copyCommandBuffer;
    if (vkAllocateCommandBuffers(m_device, &allocateInfo, &copyCommandBuffer) != VK_SUCCESS) {
        LogError("VulkanContext::copyBuffer(): could not create command buffer for copying.\n");
        return false;
    }

    AT_SCOPE_EXIT([&] {
        vkFreeCommandBuffers(m_device, m_transientCommandPool, 1, &copyCommandBuffer);
    });

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(copyCommandBuffer, &beginInfo) != VK_SUCCESS) {
        LogError("VulkanContext::copyBuffer(): could not begin the copy command buffer.\n");
        return false;
    }

    VkBufferCopy bufferCopyRegion = {};
    bufferCopyRegion.size = size;
    bufferCopyRegion.srcOffset = 0;
    bufferCopyRegion.dstOffset = 0;

    vkCmdCopyBuffer(copyCommandBuffer, source, destination, 1, &bufferCopyRegion);

    if (vkEndCommandBuffer(copyCommandBuffer) != VK_SUCCESS) {
        LogError("VulkanContext::copyBuffer(): could not end the copy command buffer.\n");
        return false;
    }

    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &copyCommandBuffer;

    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        LogError("VulkanContext::copyBuffer(): could not submit the queue for copying.\n");
        return false;
    }

    if (vkQueueWaitIdle(m_graphicsQueue) != VK_SUCCESS) {
        LogError("VulkanContext::copyBuffer(): error waiting for the queue to be idle after submitting.\n");
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

void VulkanContext::createTheDrawingStuff(VkFormat finalTargetFormat, VkExtent2D finalTargetExtent, const std::vector<VkImageView>& swapchainImageViews)
{
    m_exAspectRatio = float(finalTargetExtent.width) / float(finalTargetExtent.height);

    VkDescriptorSetLayoutBinding cameraStateUboLayoutBinding = {};
    cameraStateUboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    cameraStateUboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    cameraStateUboLayoutBinding.binding = 0;
    cameraStateUboLayoutBinding.descriptorCount = 1;
    cameraStateUboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    descriptorSetLayoutCreateInfo.bindingCount = 1;
    descriptorSetLayoutCreateInfo.pBindings = &cameraStateUboLayoutBinding;
    ASSERT(vkCreateDescriptorSetLayout(m_device, &descriptorSetLayoutCreateInfo, nullptr, &m_exDescriptorSetLayout) == VK_SUCCESS);

    VkVertexInputBindingDescription bindingDescription = {};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(mesh::common::Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions {};

    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(mesh::common::Vertex, position);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(mesh::common::Vertex, color);

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

        VkAttachmentReference colorAttachmentRef = {};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;

        VkRenderPassCreateInfo renderPassCreateInfo = {};
        renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassCreateInfo.attachmentCount = 1;
        renderPassCreateInfo.pAttachments = &colorAttachment;
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
        pipelineCreateInfo.pDepthStencilState = nullptr;
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
        VkImageView attachments[] = { swapchainImageViews[it] };

        VkFramebufferCreateInfo framebufferCreateInfo = {};
        framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferCreateInfo.renderPass = m_exRenderPass;
        framebufferCreateInfo.attachmentCount = 1;
        framebufferCreateInfo.pAttachments = attachments;
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
        VkDescriptorPoolSize descriptorPoolSize = {};
        descriptorPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorPoolSize.descriptorCount = numSwapchainImages;

        VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        descriptorPoolCreateInfo.poolSizeCount = 1;
        descriptorPoolCreateInfo.pPoolSizes = &descriptorPoolSize;
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

            VkWriteDescriptorSet writeDescriptorSet = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            writeDescriptorSet.dstSet = m_exDescriptorSets[i];
            writeDescriptorSet.dstBinding = 0;
            writeDescriptorSet.dstArrayElement = 0;

            writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writeDescriptorSet.descriptorCount = 1;
            writeDescriptorSet.pBufferInfo = &descriptorBufferInfo;
            writeDescriptorSet.pImageInfo = nullptr;
            writeDescriptorSet.pTexelBufferView = nullptr;

            vkUpdateDescriptorSets(m_device, 1, &writeDescriptorSet, 0, nullptr);
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
        { vec3(-0.5, -0.5, 0), vec3(1, 0, 0) },
        { vec3(0.5, -0.5, 0), vec3(0, 1, 0) },
        { vec3(0.5, 0.5, 0), vec3(0, 0, 1) },
        { vec3(-0.5, 0.5, 0), vec3(1, 1, 1) }
    };
    std::vector<uint16_t> indices = {
        0, 1, 2,
        2, 3, 0
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
            VkRenderPassBeginInfo renderPassBeginInfo = {};
            renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassBeginInfo.renderPass = m_exRenderPass;
            renderPassBeginInfo.framebuffer = m_targetFramebuffers[it];
            renderPassBeginInfo.renderArea.offset = { 0, 0 };
            renderPassBeginInfo.renderArea.extent = finalTargetExtent;
            VkClearValue clearColor = { 1.0f, 0.0f, 1.0f, 1.0f };
            renderPassBeginInfo.clearValueCount = 1;
            renderPassBeginInfo.pClearValues = &clearColor;

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
    cameraState.view_from_world = mathkit::lookAt({ 0, 2, -4 }, { 0, 0, 0 });
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
