#pragma once

#include "../Backend.h"
#include "VulkanQueueInfo.h"
#include "rendering/App.h"
#include "rendering/StaticResourceManager.h"
#include "utility/FrameAllocator.h"
#include <array>

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
    VkBuffer buffer(const Buffer&);

    struct TextureInfo;
    void newTexture(const Texture2D&);
    void deleteTexture(const Texture2D&);
    void updateTexture(const TextureUpdateFromFile&);
    //void updateTexture(const TextureUpdateFromData&);
    TextureInfo& textureInfo(const Texture2D&);

    struct RenderTargetInfo;
    void newRenderTarget(const RenderTarget&);
    void deleteRenderTarget(const RenderTarget&);
    void setupWindowRenderTargets();
    void destroyWindowRenderTargets();
    RenderTargetInfo& renderTargetInfo(const RenderTarget&);

    struct RenderStateInfo;
    void newRenderState(const RenderState&, uint32_t swapchainImageIndex);
    void deleteRenderState(const RenderState&);
    RenderStateInfo& renderStateInfo(const RenderState&);

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

    VkBuffer createBuffer(VkDeviceSize size, VkBufferUsageFlags, VkMemoryPropertyFlags, VkDeviceMemory&);
    bool copyBuffer(VkBuffer source, VkBuffer destination, VkDeviceSize size) const;
    bool setBufferMemoryDirectly(VkDeviceMemory, const void* data, VkDeviceSize size, VkDeviceSize offset = 0);
    bool setBufferDataUsingStagingBuffer(VkBuffer, const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

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

    std::vector<VkFramebuffer > m_swapchainFramebuffers {};
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
        // FIXME: At some later point in time, we should have a common device memory
        //  e.g. one for all vertex buffers, and here we keep a reference to that one
        //  and some offset and size. But for now every buffer has its own memory.
        std::optional<VkDeviceMemory> memory {};
    };

    struct TextureInfo {
        VkImage image {};
        std::optional<VkDeviceMemory> memory {};

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

    std::vector<BufferInfo> m_bufferInfos {};
    std::vector<TextureInfo> m_textureInfos {};
    std::vector<RenderTargetInfo> m_renderTargetInfos {};
    std::vector<RenderStateInfo> m_renderStateInfos {};

    Texture2D m_swapchainDepthTexture {};
    std::vector<Texture2D> m_swapchainColorTextures {};
    std::vector<RenderTarget> m_swapchainRenderTargets {};
};
