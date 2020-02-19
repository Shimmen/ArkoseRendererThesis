#pragma once

#include <optional>
#include <vector>
#include <vulkan/vulkan.h>

struct GLFWwindow;

class VulkanCore {
public:
    VulkanCore(GLFWwindow*, bool debugModeEnabled);
    ~VulkanCore();

    VkSurfaceFormatKHR pickBestSurfaceFormat() const;
    VkPresentModeKHR pickBestPresentMode() const;
    VkExtent2D pickBestSwapchainExtent() const;

private:
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugMessageCallback(VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT,
                                                               const VkDebugUtilsMessengerCallbackDataEXT*, void* userData);
    VkDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo() const;
    VkDebugUtilsMessengerEXT createDebugMessenger(VkInstance, VkDebugUtilsMessengerCreateInfoEXT*) const;

    VkPhysicalDevice pickBestPhysicalDevice() const;
    VkInstance createInstance(VkDebugUtilsMessengerCreateInfoEXT*) const;
    VkDevice createDevice(VkPhysicalDevice);

    void findQueueFamilyIndices(VkPhysicalDevice, VkSurfaceKHR);
    bool hasCombinedGraphicsComputeQueue() const;

    std::vector<const char*> instanceExtensions() const;
    bool verifyValidationLayerSupport() const;

private:
    GLFWwindow* m_window;

    bool m_debugModeEnabled;
    std::optional<VkDebugUtilsMessengerEXT> m_messenger {};

    VkInstance m_instance {};
    std::vector<const char*> m_activeValidationLayers {};

    VkPhysicalDevice m_physicalDevice {};
    VkDevice m_device {};

    VkSurfaceKHR m_surface {};
    VkSurfaceCapabilitiesKHR m_surfaceCapabilities {};

    uint32_t m_graphicsQueueFamilyIndex;
    uint32_t m_computeQueueFamilyIndex;
    uint32_t m_presentQueueFamilyIndex;
};
