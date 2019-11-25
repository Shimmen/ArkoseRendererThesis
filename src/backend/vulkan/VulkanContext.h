#pragma once

#include "VulkanQueueInfo.h"
#include "common-vk.h"
#include <functional>
#include <string>
#include <vector>

struct ManagedBuffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
};

struct ManagedImage {
    VkSampler sampler;
    VkImageView view;
    VkImage image;
    VkDeviceMemory memory;
};

struct BufferRange {
    uint32_t start;
    uint32_t size;
};

class VulkanContext {
public:
    VulkanContext(VkPhysicalDevice, VkDevice, VulkanQueueInfo);
    ~VulkanContext();

    void createTheDrawingStuff(VkFormat finalTargetFormat, VkExtent2D finalTargetExtent, const std::vector<VkImageView>& swapchainImageViews, VkImageView depthImageView, VkFormat depthFormat);
    void destroyTheDrawingStuff();
    void timestepForTheDrawingStuff(uint32_t index);

    void submitQueue(uint32_t imageIndex, VkSemaphore* waitFor, VkSemaphore* signal, VkFence* inFlight);

    bool issueSingleTimeCommand(const std::function<void(VkCommandBuffer)>& callback) const;

    VkBuffer createBuffer(VkDeviceSize size, VkBufferUsageFlags, VkMemoryPropertyFlags, VkDeviceMemory&);
    bool copyBuffer(VkBuffer source, VkBuffer destination, VkDeviceSize size) const;
    bool setBufferMemoryDirectly(VkDeviceMemory, const void* data, VkDeviceSize size, VkDeviceSize offset = 0);
    bool setBufferDataUsingStagingBuffer(VkBuffer, const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

    template<typename T>
    VkBuffer createDeviceLocalBuffer(const std::vector<T>& data, VkBufferUsageFlags);
    VkBuffer createDeviceLocalBuffer(VkDeviceSize, const void* data, VkBufferUsageFlags);

    VkImage createImage2D(uint32_t width, uint32_t height, VkFormat, VkImageUsageFlags, VkMemoryPropertyFlags, VkDeviceMemory&, VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL);
    VkImageView createImageView2D(VkImage, VkFormat, VkImageAspectFlags) const;

    bool transitionImageLayout(VkImage, VkFormat, VkImageLayout oldLayout, VkImageLayout newLayout) const;
    bool copyBufferToImage(VkBuffer, VkImage, uint32_t width, uint32_t height) const;

    ManagedImage createImageViewFromImagePath(const std::string& imagePath);

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
    std::vector<ManagedImage> m_managedImages {};

    //

    VulkanQueueInfo m_queueInfo {};
    VkQueue m_graphicsQueue {};

    VkCommandPool m_commandPool {};
    std::vector<VkCommandBuffer> m_commandBuffers {};

    // FIXME: This is all stuff specific for rendering the example triangle
    float m_exAspectRatio {};
    std::vector<VkBuffer> m_exCameraStateBuffers {};
    std::vector<VkDeviceMemory> m_exCameraStateBufferMemories {};
    VkDescriptorPool m_exDescriptorPool {};
    VkDescriptorSetLayout m_exDescriptorSetLayout {};
    std::vector<VkDescriptorSet> m_exDescriptorSets {};
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
