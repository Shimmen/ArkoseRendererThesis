#pragma once

#include "../Backend.h"
#include "VulkanQueueInfo.h"
#include "rendering/App.h"
#include "rendering/StaticResourceManager.h"
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

    void createStaticResources(StaticResourceManager&) override;
    void destroyStaticResources(StaticResourceManager&) override;

    bool executeFrame(double elapsedTime, double deltaTime) override;

private:
    ///////////////////////////////////////////////////////////////////////////
    /// Rendering of a single frame

    void submitQueue(uint32_t imageIndex, VkSemaphore* waitFor, VkSemaphore* signal, VkFence* inFlight);

    void timeStepForFrame(uint32_t relFrameIndex, double elapsedTime, double deltaTime);
    void reconstructPipeline(GpuPipeline&, const ApplicationState&);

    ///////////////////////////////////////////////////////////////////////////
    /// Command translation & resource management

    void translateRenderPass(VkCommandBuffer, const RenderPass&, const ResourceManager&);
    void translateDrawIndexed(VkCommandBuffer, const ResourceManager&, const CmdDrawIndexed&);

    // TODO: How are we going to keep track of the multiple separate sets of resources for the different swapchain images??
    //  We probably only want to call app.createPipeline() once and only keep one GpuPipeline. So how should it be managed?
    //  Additionally, remember that there is a resource manager connected to the app itself. Some shared stuff, like vertex
    //  buffers should have only one copy on the GPU, but others, like a uniform buffer, needs multiple copies.

    void newBuffer(const Buffer&);
    void deleteBuffer(const Buffer&);
    void updateBuffer(const BufferUpdate&);
    VkBuffer buffer(const Buffer&);

    void newTexture(const Texture2D&);
    void deleteTexture(const Texture2D&);
    void updateTexture(const TextureUpdateFromFile&);
    //void updateTexture(const TextureUpdateFromData&);
    VkImage image(const Texture2D&);

    void newRenderTarget(const RenderTarget&);
    void deleteRenderTarget(const RenderTarget&);
    void setupWindowRenderTargets();
    void destroyWindowRenderTargets();

    void newRenderState(const RenderState&);
    void deleteRenderState(const RenderState&);

    ///////////////////////////////////////////////////////////////////////////
    /// Swapchain management

    void createAndSetupSwapchain(VkPhysicalDevice, VkDevice, VkSurfaceKHR);
    void destroySwapchain();
    void recreateSwapchain();

    ///////////////////////////////////////////////////////////////////////////
    /// Temporary drawing - TODO: remove these

    void createTheDrawingStuff(VkFormat finalTargetFormat, VkExtent2D finalTargetExtent, const std::vector<VkImageView>& swapchainImageViews, VkImageView depthImageView, VkFormat depthFormat);
    void destroyTheDrawingStuff();

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

    Extent2D m_swapchainExtent {};
    uint32_t m_numSwapchainImages {};

    VkFormat m_swapchainImageFormat {};
    std::vector<VkImage> m_swapchainImages {};
    std::vector<VkImageView> m_swapchainImageViews {};

    VkFormat m_depthImageFormat {};
    VkImage m_depthImage {};
    VkImageView m_depthImageView {};
    VkDeviceMemory m_depthImageMemory {};

    //

    static constexpr size_t maxFramesInFlight { 2 };
    size_t m_currentFrameIndex { 0 };

    std::array<VkSemaphore, maxFramesInFlight> m_imageAvailableSemaphores {};
    std::array<VkSemaphore, maxFramesInFlight> m_renderFinishedSemaphores {};
    std::array<VkFence, maxFramesInFlight> m_inFlightFrameFences {};

    ///////////////////////////////////////////////////////////////////////////
    /// Resource & resource management members

    std::vector<std::unique_ptr<ResourceManager>> m_frameResourceManagers {};

    VkQueue m_graphicsQueue {};

    VkCommandPool m_commandPool {};
    VkCommandPool m_transientCommandPool {};

    GpuPipeline m_gpuPipeline {};

    std::vector<VkCommandBuffer> m_commandBuffers {};

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
    std::vector<RenderTargetInfo> m_windowRenderTargetInfos {};
    std::vector<RenderStateInfo> m_renderStateInfos {};

    ///////////////////////////////////////////////////////////////////////////
    /// Extra stuff that shouldn't be here at all - TODO: remove this

    // FIXME: This is all stuff specific for rendering the example triangle
    std::vector<Buffer> m_exCameraStateBuffers {};

    VkDescriptorPool m_exDescriptorPool {};
    VkDescriptorSetLayout m_exDescriptorSetLayout {};
    std::vector<VkDescriptorSet> m_exDescriptorSets {};
    VkPipeline m_exGraphicsPipeline {};
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
