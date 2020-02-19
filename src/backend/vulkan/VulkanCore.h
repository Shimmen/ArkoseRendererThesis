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

    VkQueue getPresentQueue() const;
    VkQueue getGraphicsQueue() const;
    bool hasCombinedGraphicsComputeQueue() const;

    const VkInstance& instance() const { return m_instance; }
    const VkPhysicalDevice& physicalDevice() const { return m_physicalDevice; }
    const VkSurfaceKHR& surface() const { return m_surface; }
    const VkDevice& device() const { return m_device; }

private:
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugMessageCallback(VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT,
                                                               const VkDebugUtilsMessengerCallbackDataEXT*, void* userData);
    VkDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo() const;
    VkDebugUtilsMessengerEXT createDebugMessenger(VkInstance, VkDebugUtilsMessengerCreateInfoEXT*) const;

    VkPhysicalDevice pickBestPhysicalDevice() const;
    VkInstance createInstance(VkDebugUtilsMessengerCreateInfoEXT*) const;
    VkDevice createDevice(VkPhysicalDevice);

    void findQueueFamilyIndices(VkPhysicalDevice, VkSurfaceKHR);

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

public:
    uint32_t m_graphicsQueueFamilyIndex;
    uint32_t m_computeQueueFamilyIndex;
    uint32_t m_presentQueueFamilyIndex;
};
