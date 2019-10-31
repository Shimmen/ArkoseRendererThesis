#pragma once

#include "common-vk.h"

struct SelfContainedBuffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
};

struct BufferRange {
    uint32_t start;
    uint32_t size;
};

class VulkanContext {
public:
    explicit VulkanContext(VkPhysicalDevice, VkDevice);
    ~VulkanContext();

    void createTheDrawingStuff(VkFormat finalTargetFormat, VkExtent2D finalTargetExtent, const std::vector<VkImageView>& swapchainImageViews);
    void destroyTheDrawingStuff();

    void submitQueue(uint32_t imageIndex, VkSemaphore* waitFor, VkSemaphore* signal, VkFence* inFlight);

    template<typename T>
    SelfContainedBuffer createBufferWithHostVisibleMemory(const std::vector<T>& data, VkBufferUsageFlags);
    SelfContainedBuffer createBufferWithHostVisibleMemory(size_t size, void* data, VkBufferUsageFlags);

    //BufferRange createBuffer(size_t size, ... todo);

    // TODO: Work towards removing this. Or rather, this function should be replaced by an application
    //  designing its own rendering pipeline & shader & fixed state & stuff stuff!
    //void createTheDrawingToScreenStuff(VkFormat finalTargetFormat, VkExtent2D finalTargetExtents);

private:
    [[nodiscard]] uint32_t findAppropriateMemory(uint32_t typeBits, VkMemoryPropertyFlags) const;

    VkPhysicalDevice m_physicalDevice;
    VkDevice m_device;

    std::vector<VkFramebuffer> m_targetFramebuffers {};

    VkCommandPool m_transientCommandPool {};

    //VkDeviceMemory m_masterMemory {};
    //VkBuffer m_masterBuffer {};

    std::vector<SelfContainedBuffer> m_selfContainedBuffers {};

    //

    VkQueue m_graphicsQueue {};

    VkCommandPool m_commandPool {};
    std::vector<VkCommandBuffer> m_commandBuffers {};

    // FIXME: This is all stuff specific for rendering the example triangle
    VkPipeline m_exGraphicsPipeline {};
    VkRenderPass m_exRenderPass {};
    VkPipelineLayout m_exPipelineLayout {};
};

template<typename T>
SelfContainedBuffer VulkanContext::createBufferWithHostVisibleMemory(const std::vector<T>& data, VkBufferUsageFlags usage)
{
    size_t numBytes = data.size() * sizeof(data[0]);
    return createBufferWithHostVisibleMemory(numBytes, data.data(), usage);
}
