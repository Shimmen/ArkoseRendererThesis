#pragma once

#include "common-vk.h"

struct ManagedBuffer {
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

    VkBuffer createBuffer(VkDeviceSize size, VkBufferUsageFlags, VkMemoryPropertyFlags, VkDeviceMemory&);
    bool copyBuffer(VkBuffer source, VkBuffer destination, VkDeviceSize size) const;
    bool setBufferMemoryDirectly(VkDeviceMemory, const void* data, VkDeviceSize size, VkDeviceSize offset = 0);
    bool setBufferDataUsingStagingBuffer(VkBuffer, const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

    template<typename T>
    VkBuffer createDeviceLocalBuffer(const std::vector<T>& data, VkBufferUsageFlags);
    VkBuffer createDeviceLocalBuffer(size_t size, const void* data, VkBufferUsageFlags);

private:
    [[nodiscard]] uint32_t findAppropriateMemory(uint32_t typeBits, VkMemoryPropertyFlags) const;

    VkPhysicalDevice m_physicalDevice;
    VkDevice m_device;

    std::vector<VkFramebuffer> m_targetFramebuffers {};

    VkCommandPool m_transientCommandPool {};

    //VkDeviceMemory m_masterMemory {};
    //VkBuffer m_masterBuffer {};

    // Buffers in this list will be destroyed and their memory freed when the context is destroyed
    std::vector<ManagedBuffer> m_managedBuffers {};

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
VkBuffer VulkanContext::createDeviceLocalBuffer(const std::vector<T>& data, VkBufferUsageFlags usage)
{
    size_t numBytes = data.size() * sizeof(data[0]);
    const void* dataPointer = static_cast<const void*>(data.data());
    return createDeviceLocalBuffer(numBytes, dataPointer, usage);
}
