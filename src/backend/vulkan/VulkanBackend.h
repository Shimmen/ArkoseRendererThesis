#pragma once

#include "../Backend.h"
#include "VulkanQueueInfo.h"
#include "rendering/App.h"
#include <array>

#include <vulkan/vulkan.h>

struct GLFWwindow;

constexpr bool vulkanDebugMode = true;

class VulkanBackend final : public Backend {
public:
    explicit VulkanBackend(GLFWwindow*);
    ~VulkanBackend() override;

    VulkanBackend(VulkanBackend&&) = default;
    VulkanBackend(VulkanBackend&) = delete;
    VulkanBackend& operator=(VulkanBackend&) = delete;

    ///////////////////////////////////////////////////////////////////////////
    /// Public backend API

    bool executeFrame(double elapsedTime, double deltaTime) override;

private:
    ///////////////////////////////////////////////////////////////////////////
    /// Command translation & resource management

    void translateRenderPass(VkCommandBuffer, const RenderPass&, const ResourceManager&);
    void translateDrawIndexed(VkCommandBuffer, const ResourceManager&, const CmdDrawIndexed&);

    void newBuffer(Buffer&);
    VkBuffer buffer(const Buffer&);

    void newFramebuffer(RenderTarget&);
    VkFramebuffer framebuffer(const RenderTarget&);

    struct RenderPassInfo {
        VkRenderPass renderPass;
        VkPipeline pipeline;
        VkPipelineLayout pipelineLayout;
    };

    void newRenderPass(RenderPass&);
    const RenderPassInfo& renderPassInfo(const RenderPass&);

    struct PipelineInfo {
        VkDescriptorSetLayout descriptorSetLayout;
        VkPipelineLayout pipelineLayout;
    };

    void recordCommandBuffers(VkFormat finalTargetFormat, VkExtent2D finalTargetExtent, const std::vector<VkImageView>& swapchainImageViews, VkImageView depthImageView, VkFormat depthFormat);
    void newFrame(uint32_t relFrameIndex, float totalTime, float deltaTime);

    ///////////////////////////////////////////////////////////////////////////
    /// Temporary drawing - TODO: remove these

    void createTheDrawingStuff(VkFormat finalTargetFormat, VkExtent2D finalTargetExtent, const std::vector<VkImageView>& swapchainImageViews, VkImageView depthImageView, VkFormat depthFormat);
    void destroyTheDrawingStuff();
    void timestepForTheDrawingStuff(uint32_t index);

    ///////////////////////////////////////////////////////////////////////////
    ///

    void submitQueue(uint32_t imageIndex, VkSemaphore* waitFor, VkSemaphore* signal, VkFence* inFlight);

    void createAndSetupSwapchain(VkPhysicalDevice, VkDevice, VkSurfaceKHR);
    void destroySwapchain();
    void recreateSwapchain();

    ///////////////////////////////////////////////////////////////////////////
    /// Internal and low level Vulkan resource API. Maybe to be removed at some later time.

    [[nodiscard]] uint32_t findAppropriateMemory(uint32_t typeBits, VkMemoryPropertyFlags) const;

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

    struct ManagedImage;
    ManagedImage createImageViewFromImagePath(const std::string& imagePath);

    ///////////////////////////////////////////////////////////////////////////
    /// Utilities for setting up the backend

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugMessageCallback(VkDebugUtilsMessageSeverityFlagBitsEXT,
        VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT*, void* userData);
    [[nodiscard]] VkDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo() const;
    [[nodiscard]] VkDebugUtilsMessengerEXT createDebugMessenger(VkInstance, VkDebugUtilsMessengerCreateInfoEXT*) const;
    void destroyDebugMessenger(VkInstance, VkDebugUtilsMessengerEXT) const;

    [[nodiscard]] std::vector<const char*> requiredInstanceExtensions() const;
    [[nodiscard]] std::vector<const char*> requiredValidationLayers() const;
    [[nodiscard]] bool checkValidationLayerSupport(const std::vector<const char*>&) const;

    [[nodiscard]] VulkanQueueInfo findQueueFamilyIndices(VkPhysicalDevice, VkSurfaceKHR);
    [[nodiscard]] VkPhysicalDevice pickBestPhysicalDevice(VkInstance, VkSurfaceKHR) const;
    [[nodiscard]] VkSurfaceFormatKHR pickBestSurfaceFormat(VkPhysicalDevice, VkSurfaceKHR) const;
    [[nodiscard]] VkPresentModeKHR pickBestPresentMode(VkPhysicalDevice, VkSurfaceKHR) const;
    [[nodiscard]] VkExtent2D pickBestSwapchainExtent(VkSurfaceCapabilitiesKHR, GLFWwindow*) const;

    [[nodiscard]] VkInstance createInstance(VkDebugUtilsMessengerCreateInfoEXT*) const;
    [[nodiscard]] VkSurfaceKHR createSurface(VkInstance, GLFWwindow*) const;
    [[nodiscard]] VkDevice createDevice(VkPhysicalDevice, VkSurfaceKHR);
    void createSemaphoresAndFences(VkDevice);

    ///////////////////////////////////////////////////////////////////////////
    /// Common backend members

    VkInstance m_instance {};
    VkDebugUtilsMessengerEXT m_messenger {};
    VkPhysicalDevice m_physicalDevice {};

    VkDevice m_device {};
    VulkanQueueInfo m_queueInfo {};

    ///////////////////////////////////////////////////////////////////////////
    /// Window and swapchain related members

    GLFWwindow* m_window;
    bool m_unhandledWindowResize { false };

    VkSurfaceKHR m_surface {};
    VkSwapchainKHR m_swapchain {};
    VkQueue m_presentQueue {};

    uint32_t m_numSwapchainImages {};
    std::vector<VkImage> m_swapchainImages {};
    std::vector<VkImageView> m_swapchainImageViews {};

    // These render to the swapchain, but they are also command buffer specific, so maybe they shouldn't be here..
    std::vector<VkFramebuffer> m_targetFramebuffers {};

    static constexpr size_t maxFramesInFlight { 2 };
    size_t m_currentFrameIndex { 0 };

    std::array<VkSemaphore, maxFramesInFlight> m_imageAvailableSemaphores {};
    std::array<VkSemaphore, maxFramesInFlight> m_renderFinishedSemaphores {};
    std::array<VkFence, maxFramesInFlight> m_inFlightFrameFences {};

    // Swapchain depth image, for when applicable
    VkImage m_depthImage {};
    VkImageView m_depthImageView {};
    VkDeviceMemory m_depthImageMemory {};

    ///////////////////////////////////////////////////////////////////////////
    /// Resource & resource management members

    VkQueue m_graphicsQueue {};

    VkCommandPool m_commandPool {};
    VkCommandPool m_transientCommandPool {};

    std::vector<VkCommandBuffer> m_commandBuffers {};

    struct BufferInfo {
        VkBuffer buffer {};
        std::optional<VkDeviceMemory> memory {};
    };

    std::vector<BufferInfo> m_bufferInfos {};
    std::vector<VkFramebuffer> m_framebuffers {};
    std::vector<VkRenderPass> m_renderPasses {};
    std::vector<RenderPassInfo> m_renderPassInfos {};

    ///////////////////////////////////////////////////////////////////////////
    /// Extra stuff that shouldn't be here at all - TODO: remove this

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

    // Buffers in this list will be destroyed and their memory freed when the context is destroyed
    // FIXME: This is also kind of specific to the example triangle. When the resource management is in place this is redundant
    std::vector<ManagedBuffer> m_managedBuffers {};
    std::vector<ManagedImage> m_managedImages {};
};

template<typename T>
VkBuffer VulkanBackend::createDeviceLocalBuffer(const std::vector<T>& data, VkBufferUsageFlags usage)
{
    size_t numBytes = data.size() * sizeof(data[0]);
    const void* dataPointer = static_cast<const void*>(data.data());
    return createDeviceLocalBuffer(numBytes, dataPointer, usage);
}
