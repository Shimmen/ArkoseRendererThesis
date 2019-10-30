#pragma once

#include "../Backend.h"
#include "common-vk.h"
#include "common.h"

struct GLFWwindow;

class VulkanBackend final : public Backend {
public:
    explicit VulkanBackend(GLFWwindow* window);
    ~VulkanBackend() override;

    bool compileCommandQueue(const CommandQueue&) override;
    bool executeFrame() override;

    ShaderID loadShader(const std::string& shaderName) override;

private:
    [[nodiscard]] std::string fileNameForShaderName(const std::string&) const;
    [[nodiscard]] VkShaderStageFlagBits vulkanShaderShaderStageFlag(ShaderStageType) const;

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugMessageCallback(VkDebugUtilsMessageSeverityFlagBitsEXT,
        VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT*, void* userData);
    [[nodiscard]] VkDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo() const;
    [[nodiscard]] VkDebugUtilsMessengerEXT createDebugMessenger(VkInstance, VkDebugUtilsMessengerCreateInfoEXT*) const;
    void destroyDebugMessenger(VkInstance, VkDebugUtilsMessengerEXT) const;

    [[nodiscard]] std::vector<const char*> requiredInstanceExtensions() const;
    [[nodiscard]] std::vector<const char*> requiredValidationLayers() const;
    [[nodiscard]] bool checkValidationLayerSupport(const std::vector<const char*>&) const;

    void findQueueFamilyIndices(VkPhysicalDevice, VkSurfaceKHR);
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

    // TODO: Work towards removing this. Or rather, this function should be replaced by an application
    //  designing its own rendering pipeline & shader & fixed state & stuff stuff!
    void createTheRemainingStuff(VkFormat finalTargetFormat, VkExtent2D finalTargetExtents);

private:
    GLFWwindow* m_window;
    VkSurfaceKHR m_surface;
    mutable bool m_unhandledWindowResize { false };

    VkInstance m_instance;
    VkDebugUtilsMessengerEXT m_messenger;
    VkPhysicalDevice m_physicalDevice;
    VkDevice m_device;

    uint32_t m_graphicsQueueFamilyIndex { UINT32_MAX };
    uint32_t m_computeQueueFamilyIndex { UINT32_MAX };
    uint32_t m_presentQueueFamilyIndex { UINT32_MAX };

    VkQueue m_graphicsQueue;
    VkQueue m_computeQueue;
    VkQueue m_presentQueue;

    VkCommandPool m_commandPool;
    std::vector<VkCommandBuffer> m_commandBuffers;

    static constexpr size_t maxFramesInFlight = 2;
    mutable size_t m_currentFrameIndex = 0;

    std::array<VkSemaphore, maxFramesInFlight> m_imageAvailableSemaphores;
    std::array<VkSemaphore, maxFramesInFlight> m_renderFinishedSemaphores;
    std::array<VkFence, maxFramesInFlight> m_inFlightFrameFences;

    //

    VkSwapchainKHR m_swapchain;

    uint32_t m_numSwapchainImages;
    std::vector<VkImageView> m_swapchainImageViews;
    std::vector<VkFramebuffer> m_swapchainFramebuffers;

    //

    // FIXME: This is all stuff specific for rendering the example triangle
    VkPipeline m_exGraphicsPipeline;
    VkRenderPass m_exRenderPass;
    VkPipelineLayout m_exPipelineLayout;

    //

    //std::vector<VkShaderModule> m_shaderModules {};
    //std::unordered_map<std::string, ShaderID> m_shaderIdForName {};
};
