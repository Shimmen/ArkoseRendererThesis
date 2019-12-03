#pragma once

#include "../Backend.h"
#include "VulkanQueueInfo.h"
#include "rendering/App.h"
#include <array>

#include <vulkan/vulkan.h>

struct GLFWwindow;

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

constexpr bool vulkanDebugMode = true;

class VulkanBackend final : public Backend {
public:
    explicit VulkanBackend(App&, GLFWwindow*);
    ~VulkanBackend() override;

    VulkanBackend(VulkanBackend&&) = default;
    VulkanBackend(VulkanBackend&) = delete;
    VulkanBackend& operator=(VulkanBackend&) = delete;

    bool compileCommandSubmitter(const CommandSubmitter&) override;
    bool executeFrame() override;

    /////////////////////////////////////////////
    /////////////////////////////////////////////

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

    //

    void recordCommandBuffers(VkFormat finalTargetFormat, VkExtent2D finalTargetExtent, const std::vector<VkImageView>& swapchainImageViews, VkImageView depthImageView, VkFormat depthFormat);
    void newFrame(uint32_t relFrameIndex, float totalTime, float deltaTime);

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

    /////////////////////////////////////////////
    /////////////////////////////////////////////

private:
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

    void createAndSetupSwapchain(VkPhysicalDevice, VkDevice, VkSurfaceKHR);
    void destroySwapchain();
    void recreateSwapchain();

private:
    GLFWwindow* m_window;
    VkSurfaceKHR m_surface {};
    bool m_unhandledWindowResize { false };

    VkInstance m_instance {};
    VkDebugUtilsMessengerEXT m_messenger {};
    VkPhysicalDevice m_physicalDevice {};
    VkDevice m_device {};

    VulkanQueueInfo m_queueInfo {};
    VkQueue m_presentQueue {};

    static constexpr size_t maxFramesInFlight { 2 };
    size_t m_currentFrameIndex { 0 };

    std::array<VkSemaphore, maxFramesInFlight> m_imageAvailableSemaphores {};
    std::array<VkSemaphore, maxFramesInFlight> m_renderFinishedSemaphores {};
    std::array<VkFence, maxFramesInFlight> m_inFlightFrameFences {};

    //

    VkSwapchainKHR m_swapchain {};

    uint32_t m_numSwapchainImages {};
    std::vector<VkImage> m_swapchainImages {};
    std::vector<VkImageView> m_swapchainImageViews {};

    VkImage m_depthImage {};
    VkImageView m_depthImageView {};
    VkDeviceMemory m_depthImageMemory {};

    /////////////////////////////////////////////
    /////////////////////////////////////////////

    [[nodiscard]] uint32_t findAppropriateMemory(uint32_t typeBits, VkMemoryPropertyFlags) const;

    App& m_app;

    std::vector<VkFramebuffer> m_targetFramebuffers {};

    VkCommandPool m_transientCommandPool {};

    //VkDeviceMemory m_masterMemory {};
    //VkBuffer m_masterBuffer {};

    // Buffers in this list will be destroyed and their memory freed when the context is destroyed
    std::vector<ManagedBuffer> m_managedBuffers {};
    std::vector<ManagedImage> m_managedImages {};

    //

    VkQueue m_graphicsQueue {};

    VkCommandPool m_commandPool {};
    std::vector<VkCommandBuffer> m_commandBuffers {};
    //std::vector<std::unique_ptr<ResourceManager>> m_resourceManagers {};

    //

    struct BufferInfo {
        VkBuffer buffer {};
        std::optional<VkDeviceMemory> memory {};
    };

    std::vector<BufferInfo> m_bufferInfos {};
    std::vector<VkFramebuffer> m_framebuffers {};
    std::vector<VkRenderPass> m_renderPasses {};
    std::vector<RenderPassInfo> m_renderPassInfos {};

    //

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
VkBuffer VulkanBackend::createDeviceLocalBuffer(const std::vector<T>& data, VkBufferUsageFlags usage)
{
    size_t numBytes = data.size() * sizeof(data[0]);
    const void* dataPointer = static_cast<const void*>(data.data());
    return createDeviceLocalBuffer(numBytes, dataPointer, usage);
}
