#pragma once

#include "../Backend.h"
#include "VulkanQueueInfo.h"
#include "rendering/App.h"
#include "rendering/StaticResourceManager.h"
#include "utility/FrameAllocator.h"
#include "utility/PersistentIndexedList.h"
#include <array>
#include <vk_mem_alloc.h>

#include <vulkan/vulkan.h>

struct GLFWwindow;

constexpr bool vulkanDebugMode = true;

class VulkanBackend final : public Backend {
public:
    VulkanBackend(GLFWwindow*, App&);
    ~VulkanBackend();

    VulkanBackend(VulkanBackend&&) = delete;
    VulkanBackend(VulkanBackend&) = delete;
    VulkanBackend& operator=(VulkanBackend&) = delete;

    ///////////////////////////////////////////////////////////////////////////
    /// Public backend API

    bool executeFrame(double elapsedTime, double deltaTime) override;

private:
    ///////////////////////////////////////////////////////////////////////////
    /// Command translation & resource management

    void executeRenderGraph(const ApplicationState&, const RenderGraph&, uint32_t swapchainImageIndex);
    void executeSetRenderState(VkCommandBuffer, const CmdSetRenderState&, const CmdClear*);
    void executeCopyTexture(VkCommandBuffer, const CmdCopyTexture&);
    void executeDrawIndexed(VkCommandBuffer, const CmdDrawIndexed&);

    void reconstructRenderGraphResources(RenderGraph& renderGraph);
    void destroyRenderGraph(RenderGraph&);

    void createStaticResources();
    void destroyStaticResources();

    void newBuffer(const Buffer&);
    void deleteBuffer(const Buffer&);
    void updateBuffer(const BufferUpdate&);
    void updateBuffer(const Buffer& buffer, const std::byte*, size_t);

    void newTexture(const Texture&);
    void deleteTexture(const Texture&);
    void updateTexture(const TextureUpdateFromFile&);
    //void updateTexture(const TextureUpdateFromData&);

    void newRenderTarget(const RenderTarget&);
    void deleteRenderTarget(const RenderTarget&);
    void setupWindowRenderTargets();
    void destroyWindowRenderTargets();

    void newRenderState(const RenderState&, uint32_t swapchainImageIndex);
    void deleteRenderState(const RenderState&);

    ///////////////////////////////////////////////////////////////////////////
    /// Swapchain management

    void submitQueue(uint32_t imageIndex, VkSemaphore* waitFor, VkSemaphore* signal, VkFence* inFlight);

    void createAndSetupSwapchain(VkPhysicalDevice, VkDevice, VkSurfaceKHR);
    void destroySwapchain();
    [[nodiscard]] Extent2D recreateSwapchain();

    void createWindowRenderTargetFrontend();

    ///////////////////////////////////////////////////////////////////////////
    /// Internal and low level Vulkan resource API. Maybe to be removed at some later time.

    [[nodiscard]] uint32_t findAppropriateMemory(uint32_t typeBits, VkMemoryPropertyFlags) const;

    bool issueSingleTimeCommand(const std::function<void(VkCommandBuffer)>& callback) const;

    bool copyBuffer(VkBuffer source, VkBuffer destination, VkDeviceSize size) const;
    bool setBufferMemoryUsingMapping(VmaAllocation, const void* data, VkDeviceSize size);
    bool setBufferDataUsingStagingBuffer(VkBuffer, const void* data, VkDeviceSize size);

    VkImage createImage2D(uint32_t width, uint32_t height, VkFormat, VkImageUsageFlags, VkMemoryPropertyFlags, VkDeviceMemory&, VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL);
    VkImageView createImageView2D(VkImage, VkFormat, VkImageAspectFlags) const;

    bool transitionImageLayout(VkImage, VkFormat, VkImageLayout oldLayout, VkImageLayout newLayout, VkCommandBuffer* = nullptr) const;
    bool copyBufferToImage(VkBuffer, VkImage, uint32_t width, uint32_t height) const;

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

    VmaAllocator m_memoryAllocator {};

    ///////////////////////////////////////////////////////////////////////////
    /// Window and swapchain related members

    GLFWwindow* m_window;

    VkSurfaceKHR m_surface {};
    VkSwapchainKHR m_swapchain {};
    VkQueue m_presentQueue {};

    Extent2D m_swapchainExtent {};
    uint32_t m_numSwapchainImages {};

    VkFormat m_swapchainImageFormat {};
    std::vector<VkImage> m_swapchainImages {};
    std::vector<VkImageView> m_swapchainImageViews {};

    VkFormat m_depthImageFormat {};
    VkImage m_depthImage {};
    VkImageView m_depthImageView {};
    VkDeviceMemory m_depthImageMemory {};

    std::vector<VkFramebuffer> m_swapchainFramebuffers {};
    VkRenderPass m_swapchainRenderPass {};

    //

    static constexpr size_t maxFramesInFlight { 2 };
    uint32_t m_currentFrameIndex { 0 };

    std::array<VkSemaphore, maxFramesInFlight> m_imageAvailableSemaphores {};
    std::array<VkSemaphore, maxFramesInFlight> m_renderFinishedSemaphores {};
    std::array<VkFence, maxFramesInFlight> m_inFlightFrameFences {};

    ///////////////////////////////////////////////////////////////////////////
    /// Resource & resource management members

    App& m_app;

    std::unique_ptr<StaticResourceManager> m_staticResourceManager {};
    std::vector<std::unique_ptr<ResourceManager>> m_frameResourceManagers {};

    static constexpr size_t frameAllocatorSize { 2048 };
    std::vector<std::unique_ptr<FrameAllocator>> m_frameAllocators {};

    VkQueue m_graphicsQueue {};

    VkCommandPool m_renderGraphFrameCommandPool {};
    VkCommandPool m_transientCommandPool {};

    std::vector<VkCommandBuffer> m_frameCommandBuffers {};

    std::unique_ptr<RenderGraph> m_renderGraph {};

    struct BufferInfo {
        VkBuffer buffer {};
        VmaAllocation allocation {};
    };

    struct TextureInfo {
        VkImage image {};
        VmaAllocation allocation {};

        VkFormat format {};
        VkImageView view {};
        VkSampler sampler {};

        VkImageLayout currentLayout {};
    };

    struct RenderTargetInfo {
        VkFramebuffer framebuffer {};
        VkRenderPass compatibleRenderPass {};
    };

    struct RenderStateInfo {
        VkDescriptorPool descriptorPool {};
        VkDescriptorSet descriptorSet {};
        VkDescriptorSetLayout descriptorSetLayout {};
        VkPipelineLayout pipelineLayout {};
        VkPipeline pipeline {};
    };

    // (helpers for accessing from *Infos vectors)
    BufferInfo& bufferInfo(const Buffer&);
    TextureInfo& textureInfo(const Texture&);
    RenderTargetInfo& renderTargetInfo(const RenderTarget&);
    RenderStateInfo& renderStateInfo(const RenderState&);

    PersistentIndexedList<BufferInfo> m_bufferInfos {};
    PersistentIndexedList<TextureInfo> m_textureInfos {};
    PersistentIndexedList<RenderTargetInfo> m_renderTargetInfos {};
    PersistentIndexedList<RenderStateInfo> m_renderStateInfos {};

    Texture m_swapchainDepthTexture {};
    std::vector<Texture> m_swapchainColorTextures {};
    std::vector<RenderTarget> m_swapchainRenderTargets {};
};
