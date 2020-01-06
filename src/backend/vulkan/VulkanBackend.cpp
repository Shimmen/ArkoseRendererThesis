#include "VulkanBackend.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "VulkanQueueInfo.h"
#include "utility/fileio.h"
#include "utility/logging.h"
#include "utility/util.h"
#include <algorithm>
#include <cstring>
#include <stb_image.h>
#include <unordered_set>

#include "camera_state.h"

VulkanBackend::VulkanBackend(GLFWwindow* window)
    : m_window(window)
{
    glfwSetFramebufferSizeCallback(m_window, static_cast<GLFWframebuffersizefun>([](GLFWwindow* window, int, int) {
        auto self = static_cast<VulkanBackend*>(glfwGetWindowUserPointer(window));
        self->m_unhandledWindowResize = true;
    }));

    // TODO: Make the creation & deletion of this conditional! We might not always wont this (for performance)
    VkDebugUtilsMessengerCreateInfoEXT dbgMessengerCreateInfo = debugMessengerCreateInfo();

    m_instance = createInstance(&dbgMessengerCreateInfo);
    m_messenger = createDebugMessenger(m_instance, &dbgMessengerCreateInfo);

    m_surface = createSurface(m_instance, m_window);
    m_physicalDevice = pickBestPhysicalDevice(m_instance, m_surface);

    m_queueInfo = findQueueFamilyIndices(m_physicalDevice, m_surface);
    m_device = createDevice(m_physicalDevice, m_surface);
    createSemaphoresAndFences(m_device);

    vkGetDeviceQueue(m_device, m_queueInfo.presentQueueFamilyIndex, 0, &m_presentQueue);
    vkGetDeviceQueue(m_device, m_queueInfo.graphicsQueueFamilyIndex, 0, &m_graphicsQueue);

    VkCommandPoolCreateInfo poolCreateInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolCreateInfo.queueFamilyIndex = m_queueInfo.graphicsQueueFamilyIndex;
    poolCreateInfo.flags = 0u;
    if (vkCreateCommandPool(m_device, &poolCreateInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend::VulkanBackend(): could not create command pool for the graphics queue, exiting.\n");
    }

    VkCommandPoolCreateInfo transientPoolCreateInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    transientPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    transientPoolCreateInfo.queueFamilyIndex = m_queueInfo.graphicsQueueFamilyIndex;
    if (vkCreateCommandPool(m_device, &transientPoolCreateInfo, nullptr, &m_transientCommandPool) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend::VulkanBackend(): could not create transient command pool, exiting.\n");
    }

    createAndSetupSwapchain(m_physicalDevice, m_device, m_surface);
}

VulkanBackend::~VulkanBackend()
{
    // Before destroying stuff, make sure it's done with all scheduled work
    vkDeviceWaitIdle(m_device);

    destroySwapchain();

    for (const auto& buffer : m_managedBuffers) {
        vkDestroyBuffer(m_device, buffer.buffer, nullptr);
        vkFreeMemory(m_device, buffer.memory, nullptr);
    }

    for (const auto& image : m_managedImages) {
        vkDestroySampler(m_device, image.sampler, nullptr);
        vkDestroyImageView(m_device, image.view, nullptr);
        vkDestroyImage(m_device, image.image, nullptr);
        vkFreeMemory(m_device, image.memory, nullptr);
    }

    vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    vkDestroyCommandPool(m_device, m_transientCommandPool, nullptr);

    for (size_t it = 0; it < maxFramesInFlight; ++it) {
        vkDestroySemaphore(m_device, m_imageAvailableSemaphores[it], nullptr);
        vkDestroySemaphore(m_device, m_renderFinishedSemaphores[it], nullptr);
        vkDestroyFence(m_device, m_inFlightFrameFences[it], nullptr);
    }

    vkDestroyDevice(m_device, nullptr);
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    destroyDebugMessenger(m_instance, m_messenger);
    vkDestroyInstance(m_instance, nullptr);
}

std::vector<const char*> VulkanBackend::requiredInstanceExtensions() const
{
    std::vector<const char*> extensions;

    // GLFW requires a few for basic presenting etc.
    uint32_t requiredCount;
    const char** requiredExtensions = glfwGetRequiredInstanceExtensions(&requiredCount);
    while (requiredCount--) {
        extensions.emplace_back(requiredExtensions[requiredCount]);
    }

    // For debug messages etc.
    extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    return extensions;
}

std::vector<const char*> VulkanBackend::requiredValidationLayers() const
{
    std::vector<const char*> layers;

    if (vulkanDebugMode) {
        layers.emplace_back("VK_LAYER_KHRONOS_validation");
    }

    return layers;
}

bool VulkanBackend::checkValidationLayerSupport(const std::vector<const char*>& layers) const
{
    uint32_t availableLayerCount;
    vkEnumerateInstanceLayerProperties(&availableLayerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(availableLayerCount);
    vkEnumerateInstanceLayerProperties(&availableLayerCount, availableLayers.data());

    bool fullSupport = true;
    for (const char* layer : layers) {
        bool found = false;
        for (auto availableLayer : availableLayers) {
            if (std::strcmp(layer, availableLayer.layerName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            LogError("VulkanBackend::checkValidationLayerSupport(): layer '%s' is not supported.\n", layer);
            fullSupport = false;
        }
    }

    return fullSupport;
}

VkBool32 VulkanBackend::debugMessageCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageTypes, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
    LogError("VulkanBackend::debugMessageCallback(): %s\n", pCallbackData->pMessage);
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT VulkanBackend::debugMessengerCreateInfo() const
{
    VkDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };

    debugMessengerCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugMessengerCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugMessengerCreateInfo.pfnUserCallback = debugMessageCallback;
    debugMessengerCreateInfo.pUserData = nullptr;

    return debugMessengerCreateInfo;
}

VkDebugUtilsMessengerEXT VulkanBackend::createDebugMessenger(VkInstance instance, VkDebugUtilsMessengerCreateInfoEXT* createInfo) const
{
    auto createFunc = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (!createFunc) {
        LogErrorAndExit("VulkanBackend::createDebugMessenger(): could not get function 'vkCreateDebugUtilsMessengerEXT', exiting.\n");
    }

    VkDebugUtilsMessengerEXT messenger;
    if (createFunc(instance, createInfo, nullptr, &messenger) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend::createDebugMessenger(): could not create the debug messenger, exiting.\n");
    }

    return messenger;
}

void VulkanBackend::destroyDebugMessenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger) const
{
    auto destroyFunc = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (!destroyFunc) {
        LogErrorAndExit("VulkanBackend::destroyDebugMessenger(): could not get function 'vkDestroyDebugUtilsMessengerEXT', exiting.\n");
    }
    destroyFunc(instance, messenger, nullptr);
}

VulkanQueueInfo VulkanBackend::findQueueFamilyIndices(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
{
    uint32_t count;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, queueFamilies.data());

    bool foundGraphicsQueue = false;
    bool foundComputeQueue = false;
    bool foundPresentQueue = false;

    VulkanQueueInfo queueInfo {};

    for (uint32_t idx = 0; idx < count; ++idx) {
        const auto& queueFamily = queueFamilies[idx];

        if (!foundGraphicsQueue && queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queueInfo.graphicsQueueFamilyIndex = idx;
            foundGraphicsQueue = true;
        }

        if (!foundComputeQueue && queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) {
            queueInfo.computeQueueFamilyIndex = idx;
            foundComputeQueue = true;
        }

        if (!foundPresentQueue) {
            VkBool32 presentSupportForQueue;
            vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, idx, surface, &presentSupportForQueue);
            if (presentSupportForQueue) {
                queueInfo.presentQueueFamilyIndex = idx;
                foundPresentQueue = true;
            }
        }
    }

    if (!foundGraphicsQueue) {
        LogErrorAndExit("VulkanBackend::findQueueFamilyIndices(): could not find a graphics queue, exiting.\n");
    }
    if (!foundComputeQueue) {
        LogErrorAndExit("VulkanBackend::findQueueFamilyIndices(): could not find a compute queue, exiting.\n");
    }
    if (!foundPresentQueue) {
        LogErrorAndExit("VulkanBackend::findQueueFamilyIndices(): could not find a present queue, exiting.\n");
    }

    return queueInfo;
}

VkPhysicalDevice VulkanBackend::pickBestPhysicalDevice(VkInstance instance, VkSurfaceKHR surface) const
{
    uint32_t count;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (count < 1) {
        LogErrorAndExit("VulkanBackend::pickBestPhysicalDevice(): could not find any physical devices with Vulkan support, exiting.\n");
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());

    if (count > 1) {
        LogWarning("VulkanBackend::pickBestPhysicalDevice(): more than one physical device available, one will be chosen arbitrarily (FIXME!)\n");
    }

    // FIXME: Don't just pick the first one if there are more than one!
    VkPhysicalDevice physicalDevice = devices[0];

    // Verify that the physical device supports presenting
    VkBool32 presentSupport = false;
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());
    for (uint32_t it = 0; it < queueFamilyCount; ++it) {
        VkBool32 presentSupportForQueue;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, it, surface, &presentSupportForQueue);
        presentSupport = presentSupport || presentSupportForQueue;
    }
    if (!presentSupport) {
        LogErrorAndExit("VulkanBackend::pickBestPhysicalDevice(): could not find a physical device with present support, exiting.\n");
    }

    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, availableExtensions.data());

    // Verify that the physical device has swapchain support
    bool swapchainSupport = false;
    for (auto& extension : availableExtensions) {
        if (std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
            swapchainSupport = true;
            break;
        }
    }
    if (!swapchainSupport) {
        LogErrorAndExit("VulkanBackend::pickBestPhysicalDevice(): could not find a physical device with swapchain support, exiting.\n");
    }

    VkPhysicalDeviceFeatures supportedFeatures;
    vkGetPhysicalDeviceFeatures(physicalDevice, &supportedFeatures);
    if (!supportedFeatures.samplerAnisotropy) {
        LogErrorAndExit("VulkanBackend::pickBestPhysicalDevice(): could not find a physical device with sampler anisotropy support, exiting.\n");
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    LogInfo("VulkanBackend::pickBestPhysicalDevice(): using physical device '%s'\n", props.deviceName);

    return physicalDevice;
}

VkSurfaceFormatKHR VulkanBackend::pickBestSurfaceFormat(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) const
{
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, surfaceFormats.data());

    for (const auto& format : surfaceFormats) {
        // We use the *_UNORM format since "working directly with SRGB colors is a little bit challenging"
        // (https://vulkan-tutorial.com/Drawing_a_triangle/Presentation/Swap_chain). I don't really know what that's about..
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            LogInfo("VulkanBackend::pickBestSurfaceFormat(): picked optimal RGBA8 sRGB surface format.\n");
            return format;
        }
    }

    // If we didn't find the optimal one, just chose an arbitrary one
    LogInfo("VulkanBackend::pickBestSurfaceFormat(): couldn't find optimal surface format, so picked arbitrary supported format.\n");
    VkSurfaceFormatKHR format = surfaceFormats[0];

    if (format.colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        LogWarning("VulkanBackend::pickBestSurfaceFormat(): could not find a sRGB surface format, so images won't be pretty!\n");
    }

    return format;
}

VkPresentModeKHR VulkanBackend::pickBestPresentMode(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) const
{
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());

    for (const auto& mode : presentModes) {
        // Try to chose the mailbox mode, i.e. use-last-fully-generated-image mode
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            LogInfo("VulkanBackend::pickBestPresentMode(): picked optimal mailbox present mode.\n");
            return mode;
        }
    }

    // VK_PRESENT_MODE_FIFO_KHR is guaranteed to be available and it basically corresponds to normal v-sync so it's fine
    LogInfo("VulkanBackend::pickBestPresentMode(): picked standard FIFO present mode.\n");
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanBackend::pickBestSwapchainExtent(VkSurfaceCapabilitiesKHR surfaceCapabilities, GLFWwindow* window) const
{
    if (surfaceCapabilities.currentExtent.width != UINT32_MAX) {
        // The surface has specified the extent (probably to whatever the window extent is) and we should choose that
        LogInfo("VulkanBackend::pickBestSwapchainExtent(): using optimal window extents for swap chain.\n");
        return surfaceCapabilities.currentExtent;
    }

    // The drivers are flexible, so let's choose something good that is within the the legal extents
    VkExtent2D extent = {};

    int framebufferWidth, framebufferHeight;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);

    extent.width = std::clamp(static_cast<uint32_t>(framebufferWidth), surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width);
    extent.height = std::clamp(static_cast<uint32_t>(framebufferHeight), surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height);
    LogInfo("VulkanBackend::pickBestSwapchainExtent(): using specified extents (%u x %u) for swap chain.\n", extent.width, extent.height);

    return extent;
}

VkInstance VulkanBackend::createInstance(VkDebugUtilsMessengerCreateInfoEXT* debugMessengerCreateInfo) const
{
    VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName = "ArkoseRenderer";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "ArkoseRendererEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instanceCreateInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    instanceCreateInfo.pNext = debugMessengerCreateInfo;
    instanceCreateInfo.pApplicationInfo = &appInfo;

    const auto& extensions = requiredInstanceExtensions();
    instanceCreateInfo.enabledExtensionCount = extensions.size();
    instanceCreateInfo.ppEnabledExtensionNames = extensions.data();

    const auto& layers = requiredValidationLayers();
    if (!checkValidationLayerSupport(layers)) {
        LogErrorAndExit("VulkanBackend::createInstance(): there are unsupported but required validation layers, exiting.\n");
    }
    instanceCreateInfo.enabledLayerCount = layers.size();
    instanceCreateInfo.ppEnabledLayerNames = layers.data();

    VkInstance instance;
    if (vkCreateInstance(&instanceCreateInfo, nullptr, &instance) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend::createInstance(): could not create instance.\n");
    }

    return instance;
}

VkSurfaceKHR VulkanBackend::createSurface(VkInstance instance, GLFWwindow* window) const
{
    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend::createSurface(): can't create window surface, exiting.\n");
    }

    return surface;
}

VkDevice VulkanBackend::createDevice(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
{
    std::unordered_set<uint32_t> queueFamilyIndices = { m_queueInfo.graphicsQueueFamilyIndex, m_queueInfo.computeQueueFamilyIndex, m_queueInfo.presentQueueFamilyIndex };
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    const float queuePriority = 1.0f;
    for (uint32_t familyIndex : queueFamilyIndices) {
        VkDeviceQueueCreateInfo queueCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };

        queueCreateInfo.queueCount = 1;
        queueCreateInfo.queueFamilyIndex = familyIndex;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures requestedDeviceFeatures = {};
    requestedDeviceFeatures.samplerAnisotropy = VK_TRUE;

    VkDeviceCreateInfo deviceCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    deviceCreateInfo.queueCreateInfoCount = queueCreateInfos.size();
    deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
    deviceCreateInfo.pEnabledFeatures = &requestedDeviceFeatures;

    std::vector<const char*> deviceValidationLayers;
    if (vulkanDebugMode) {
        // (the existence of this extension should already have been checked!)
        deviceValidationLayers.emplace_back("VK_LAYER_KHRONOS_validation");
    }
    deviceCreateInfo.enabledLayerCount = deviceValidationLayers.size();
    deviceCreateInfo.ppEnabledLayerNames = deviceValidationLayers.data();

    // (the existence of this extension should already have been checked!)
    std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    deviceCreateInfo.enabledExtensionCount = deviceExtensions.size();
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();

    VkDevice device;
    if (vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend::createDevice(): could not create a device, exiting.\n");
    }

    return device;
}

void VulkanBackend::createSemaphoresAndFences(VkDevice device)
{
    VkSemaphoreCreateInfo semaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

    VkFenceCreateInfo fenceCreateInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    bool allSemaphoresCreatedSuccessfully = true;
    bool allFencesCreatedSuccessfully = true;

    for (size_t it = 0; it < maxFramesInFlight; ++it) {
        if (vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &m_imageAvailableSemaphores[it]) != VK_SUCCESS) {
            allSemaphoresCreatedSuccessfully = false;
            break;
        }
        if (vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &m_renderFinishedSemaphores[it]) != VK_SUCCESS) {
            allSemaphoresCreatedSuccessfully = false;
            break;
        }
        if (vkCreateFence(device, &fenceCreateInfo, nullptr, &m_inFlightFrameFences[it]) != VK_SUCCESS) {
            allFencesCreatedSuccessfully = false;
            break;
        }
    }

    if (!allSemaphoresCreatedSuccessfully) {
        LogErrorAndExit("VulkanBackend::createSemaphoresAndFences(): could not create one or more semaphores, exiting.\n");
    }
    if (!allFencesCreatedSuccessfully) {
        LogErrorAndExit("VulkanBackend::createSemaphoresAndFences(): could not create one or more fence, exiting.\n");
    }
}

int VulkanBackend::multiplicity() const
{
    ASSERT(m_numSwapchainImages > 0);
    return m_numSwapchainImages;
}

void VulkanBackend::createAndSetupSwapchain(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface)
{
    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &surfaceCapabilities) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend::createAndSetupSwapchain(): could not get surface capabilities, exiting.\n");
    }

    VkSwapchainCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    createInfo.surface = surface;

    // Request one more image than required, if possible (see https://github.com/KhronosGroup/Vulkan-Docs/issues/909 for information)
    createInfo.minImageCount = surfaceCapabilities.minImageCount + 1;
    if (surfaceCapabilities.minImageCount != 0) {
        // (max of zero means no upper limit, so don't clamp in that case)
        createInfo.minImageCount = std::min(createInfo.minImageCount, surfaceCapabilities.maxImageCount);
    }

    VkSurfaceFormatKHR surfaceFormat = pickBestSurfaceFormat(m_physicalDevice, m_surface);
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;

    VkPresentModeKHR presentMode = pickBestPresentMode(m_physicalDevice, m_surface);
    createInfo.presentMode = presentMode;

    VkExtent2D swapchainExtent = pickBestSwapchainExtent(surfaceCapabilities, m_window);
    createInfo.imageExtent = swapchainExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; // (only color for the swapchain images)

    uint32_t queueFamilyIndices[] = { m_queueInfo.graphicsQueueFamilyIndex, m_queueInfo.presentQueueFamilyIndex };
    if (!m_queueInfo.combinedGraphicsComputeQueue()) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
        createInfo.queueFamilyIndexCount = 2;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = surfaceCapabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; // opaque swapchain
    createInfo.clipped = VK_TRUE; // clip pixels obscured by other windows etc.

    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &m_swapchain) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend::createAndSetupSwapchain(): could not create swapchain, exiting.\n");
    }

    vkGetSwapchainImagesKHR(device, m_swapchain, &m_numSwapchainImages, nullptr);
    m_swapchainImages.resize(m_numSwapchainImages);
    vkGetSwapchainImagesKHR(device, m_swapchain, &m_numSwapchainImages, m_swapchainImages.data());

    m_swapchainImageViews.resize(m_numSwapchainImages);
    for (size_t i = 0; i < m_swapchainImages.size(); ++i) {

        VkImageViewCreateInfo imageViewCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };

        imageViewCreateInfo.image = m_swapchainImages[i];
        imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewCreateInfo.format = surfaceFormat.format;

        imageViewCreateInfo.components = {
            .r = VK_COMPONENT_SWIZZLE_IDENTITY,
            .g = VK_COMPONENT_SWIZZLE_IDENTITY,
            .b = VK_COMPONENT_SWIZZLE_IDENTITY,
            .a = VK_COMPONENT_SWIZZLE_IDENTITY
        };

        imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
        imageViewCreateInfo.subresourceRange.levelCount = 1;

        imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
        imageViewCreateInfo.subresourceRange.layerCount = 1;

        imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        if (vkCreateImageView(device, &imageViewCreateInfo, nullptr, &m_swapchainImageViews[i]) != VK_SUCCESS) {
            LogErrorAndExit("VulkanBackend::createAndSetupSwapchain(): could not create image view %u (out of %u), exiting.\n", i, m_numSwapchainImages);
        }
    }

    m_swapchainExtent = { swapchainExtent.width, swapchainExtent.height };
    m_swapchainImageFormat = surfaceFormat.format;
    m_depthImageFormat = VK_FORMAT_D32_SFLOAT;

    // FIXME: For now also create a depth image, but later we probably don't want one on the final presentation images
    //  so it doesn't really make sense to have it here anyway. I guess that's an excuse for the code structure.. :)
    // FIXME: Should we add an explicit image transition for the depth image..?
    m_depthImage = createImage2D(swapchainExtent.width, swapchainExtent.height, m_depthImageFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_depthImageMemory);
    m_depthImageView = createImageView2D(m_depthImage, m_depthImageFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

    setupWindowRenderTargets();

    createTheDrawingStuff(m_swapchainImageFormat, swapchainExtent, m_swapchainImageViews, m_depthImageView, m_depthImageFormat);
}

void VulkanBackend::destroySwapchain()
{
    destroyTheDrawingStuff();

    destroyWindowRenderTargets();

    vkDestroyImageView(m_device, m_depthImageView, nullptr);
    vkDestroyImage(m_device, m_depthImage, nullptr);
    vkFreeMemory(m_device, m_depthImageMemory, nullptr);

    for (size_t it = 0; it < m_numSwapchainImages; ++it) {
        vkDestroyImageView(m_device, m_swapchainImageViews[it], nullptr);
    }

    vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
}

void VulkanBackend::recreateSwapchain()
{
    while (true) {
        // As long as we are minimized, don't do anything
        int windowFramebufferWidth, windowFramebufferHeight;
        glfwGetFramebufferSize(m_window, &windowFramebufferWidth, &windowFramebufferHeight);
        if (windowFramebufferWidth == 0 || windowFramebufferHeight == 0) {
            LogInfo("VulkanBackend::recreateSwapchain(): rendering paused since there are no pixels to draw to.\n");
            glfwWaitEvents();
        } else {
            LogInfo("VulkanBackend::recreateSwapchain(): rendering resumed.\n");
            break;
        }
    }

    vkDeviceWaitIdle(m_device);

    destroySwapchain();
    createAndSetupSwapchain(m_physicalDevice, m_device, m_surface);

    m_unhandledWindowResize = false;
}

bool VulkanBackend::executeFrame(double elapsedTime, double deltaTime)
{
    uint32_t currentFrameMod = m_currentFrameIndex % maxFramesInFlight;

    if (vkWaitForFences(m_device, 1, &m_inFlightFrameFences[currentFrameMod], VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        LogError("VulkanBackend::executeFrame(): error while waiting for in-flight frame fence (frame %u).\n", m_currentFrameIndex);
    }

    uint32_t swapchainImageIndex;
    VkResult acquireResult = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, m_imageAvailableSemaphores[currentFrameMod], VK_NULL_HANDLE, &swapchainImageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        // Since we couldn't acquire an image to draw to, recreate the swapchain and report that it didn't work
        recreateSwapchain();
        return false;
    }
    if (acquireResult == VK_SUBOPTIMAL_KHR) {
        // Since we did manage to acquire an image, just roll with it for now, but it will probably resolve itself after presenting
        LogWarning("VulkanBackend::executeFrame(): next image was acquired but it's suboptimal, ignoring.\n");
    }

    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        LogError("VulkanBackend::executeFrame(): error acquiring next swapchain image.\n");
    }

    timeStepForFrame(swapchainImageIndex, elapsedTime, deltaTime);
    submitQueue(swapchainImageIndex, &m_imageAvailableSemaphores[currentFrameMod], &m_renderFinishedSemaphores[currentFrameMod], &m_inFlightFrameFences[currentFrameMod]);

    // Present results (synced on the semaphores)
    {
        VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };

        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &m_renderFinishedSemaphores[currentFrameMod];

        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &m_swapchain;
        presentInfo.pImageIndices = &swapchainImageIndex;

        VkResult presentResult = vkQueuePresentKHR(m_presentQueue, &presentInfo);

        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || m_unhandledWindowResize) {
            recreateSwapchain();
        } else if (presentResult != VK_SUCCESS) {
            LogError("VulkanBackend::executeFrame(): could not present swapchain (frame %u).\n", m_currentFrameIndex);
        }
    }

    m_currentFrameIndex += 1;
    return true;
}

void VulkanBackend::translateRenderPass(VkCommandBuffer commandBuffer, const RenderPass& renderPass, const ResourceManager& resourceManager)
{
    VkRenderPassBeginInfo renderPassBeginInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };

    Extent2D extent = renderPass.target().extent();
    renderPassBeginInfo.renderArea.extent = { extent.width(), extent.height() };
    renderPassBeginInfo.renderArea.offset = { 0, 0 };

    auto cmdIterator = renderPass.commands().begin();

    std::vector<VkClearValue> clearValues {};
    if (renderPass.commands().front()->type() == typeid(CmdClear)) {

        // TODO: Respect clear command settings!
        //auto clearCmd = dynamic_cast<CmdClear*>(renderPass.commands().front());
        //clearValues.reserve(renderPass.target().attachmentCount() + (renderPass.target().hasDepthTarget() ? 1 : 0));

        clearValues[0].color = { 1.0f, 0.0f, 1.0f, 1.0f };
        clearValues[1].depthStencil = { 1.0f, 0 };

        renderPassBeginInfo.clearValueCount = clearValues.size();
        renderPassBeginInfo.pClearValues = clearValues.data();

        // Skip the first command now, because its intent has been captured
        std::advance(cmdIterator, 1);
    }

    // TODO: The render pass, framebuffer, pipeline, and pipeline layout should be created at render pass construction.
    //  At this stage we should just perform a basic lookup to get those resources from the VulkanBackend

    // TODO: The descriptor set (and maybe the pipeline layout?!) can be tied to per frame updates, so we need to do some stuff here. Unclear..

    VulkanBackend::RenderPassInfo info = renderPassInfo(renderPass);
    renderPassBeginInfo.renderPass = info.renderPass;
    // TODO: Hmm, but the render pass is created before we establish render targets, i.e. framebuffers. Or can we let our layering take care of that somehow?
    renderPassBeginInfo.framebuffer = nullptr; //framebuffer(renderPass.target());

    vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, info.pipeline);
    //vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, info.pipelineLayout, 0, 1, &THE_DESCRIPTOR_SET_FOR_FRAME, 0, nullptr); // TODO!

    for (; cmdIterator != renderPass.commands().end(); std::advance(cmdIterator, 1)) {
        const FrontendCommand* cmd = *cmdIterator;
        auto& type = cmd->type();

        if (type == typeid(CmdDrawIndexed)) {
            translateDrawIndexed(commandBuffer, resourceManager, *dynamic_cast<const CmdDrawIndexed*>(cmd));
        } else {
            ASSERT_NOT_REACHED();
        }
    }

    vkCmdEndRenderPass(commandBuffer);
}

void VulkanBackend::translateDrawIndexed(VkCommandBuffer commandBuffer, const ResourceManager& resourceManager, const CmdDrawIndexed& command)
{
    VkBuffer vertexBuffer = buffer(command.vertexBuffer);
    VkBuffer indexBuffer = buffer(command.indexBuffer);

    VkBuffer vertexBuffers[] = { vertexBuffer };
    VkDeviceSize offsets[] = { 0 };

    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(commandBuffer, command.numIndices, 1, 0, 0, 0);
}

void VulkanBackend::newBuffer(const Buffer& buffer)
{
    VkBufferUsageFlags usageFlags = 0u;
    switch (buffer.usage()) {
    case Buffer::Usage::Vertex:
        usageFlags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        break;
    case Buffer::Usage::Index:
        usageFlags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        break;
    case Buffer::Usage::UniformBuffer:
        usageFlags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        break;
    }

    VkMemoryPropertyFlags memoryPropertyFlags = 0u;
    switch (buffer.memoryHint()) {
    case Buffer::MemoryHint::GpuOptimal:
        memoryPropertyFlags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        usageFlags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        break;
    case Buffer::MemoryHint::TransferOptimal:
        memoryPropertyFlags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        memoryPropertyFlags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        break;
    }

    VkDeviceMemory deviceMemory;
    VkBuffer vkBuffer = createBuffer(buffer.size(), usageFlags, memoryPropertyFlags, deviceMemory);

    BufferInfo bufferInfo {};
    bufferInfo.buffer = vkBuffer;
    bufferInfo.memory = deviceMemory;

    // TODO: Use free lists!
    size_t index = m_bufferInfos.size();
    m_bufferInfos.push_back(bufferInfo);
    buffer.registerBackend(backendBadge(), index);
}

void VulkanBackend::deleteBuffer(const Buffer& buffer)
{
    if (buffer.id() == Resource::NullId) {
        LogErrorAndExit("Trying to delete an already-deleted or not-yet-created buffer\n");
    }

    // TODO: When we have a free list, also maybe remove from the m_bufferInfos vector? But then we should also keep track of generations etc.
    BufferInfo& bufferInfo = m_bufferInfos[buffer.id()];
    vkDestroyBuffer(m_device, bufferInfo.buffer, nullptr);
    if (bufferInfo.memory.has_value()) {
        vkFreeMemory(m_device, bufferInfo.memory.value(), nullptr);
    }

    buffer.unregisterBackend(backendBadge());
}

void VulkanBackend::updateBuffer(const BufferUpdate& update)
{
    if (update.buffer().id() == Resource::NullId) {
        LogErrorAndExit("Trying to update an already-deleted or not-yet-created buffer\n");
    }

    BufferInfo& bufferInfo = m_bufferInfos[update.buffer().id()];

    const std::byte* data = update.data().data();
    size_t size = update.data().size();

    switch (update.buffer().memoryHint()) {
    case Buffer::MemoryHint::GpuOptimal:
        setBufferDataUsingStagingBuffer(bufferInfo.buffer, data, size);
        break;
    case Buffer::MemoryHint::TransferOptimal:
        if (!bufferInfo.memory.has_value()) {
            LogErrorAndExit("Trying to update transfer optimal buffer that doesn't own it's memory, which currently isn't upported!\n");
        }
        setBufferMemoryDirectly(bufferInfo.memory.value(), data, size);
        break;
    }
}

VkBuffer VulkanBackend::buffer(const Buffer& buffer)
{
    BufferInfo& bufferInfo = m_bufferInfos[buffer.id()];
    return bufferInfo.buffer;
}

void VulkanBackend::newTexture(const Texture2D& texture)
{
    VkFormat format;
    bool isDepthFormat = false;

    switch (texture.format()) {
    case Texture2D::Format::RGBA8:
        format = VK_FORMAT_R8G8B8A8_UNORM;
        break;
    case Texture2D::Format::Depth32F:
        format = VK_FORMAT_D32_SFLOAT;
        isDepthFormat = true;
        break;
    }

    // TODO: For now, since we don't have the information available, we always assume that all images might be sampled or used as attachments!
    //  In the future we probably want to provide this info or give hints etc. but for now this will have to do..
    VkImageUsageFlags usageFlags = 0u;
    usageFlags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (isDepthFormat) {
        usageFlags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    } else {
        usageFlags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }

    VkMemoryPropertyFlags memoryPropertyFlags = 0u;

    // TODO: For now always keep images in device local memory.
    memoryPropertyFlags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    usageFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    VkDeviceMemory deviceMemory;
    VkImage image = createImage2D(texture.extent().width(), texture.extent().height(), format, usageFlags, memoryPropertyFlags, deviceMemory);

    // TODO: Handle things like mipmaps here!
    VkImageAspectFlags aspectFlags = 0u;
    if (isDepthFormat) {
        aspectFlags |= VK_IMAGE_ASPECT_DEPTH_BIT;
    } else {
        aspectFlags |= VK_IMAGE_ASPECT_COLOR_BIT;
    }
    VkImageView imageView = createImageView2D(image, format, aspectFlags);

    VkFilter minFilter;
    switch (texture.minFilter()) {
    case Texture2D::MinFilter::Linear:
        minFilter = VK_FILTER_LINEAR;
        break;
    case Texture2D::MinFilter::Nearest:
        minFilter = VK_FILTER_NEAREST;
        break;
    }

    VkFilter magFilter;
    switch (texture.magFilter()) {
    case Texture2D::MagFilter::Linear:
        magFilter = VK_FILTER_LINEAR;
        break;
    case Texture2D::MagFilter::Nearest:
        magFilter = VK_FILTER_NEAREST;
        break;
    }

    VkSamplerCreateInfo samplerCreateInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;
    samplerCreateInfo.magFilter = magFilter;
    samplerCreateInfo.minFilter = minFilter;
    samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerCreateInfo.anisotropyEnable = VK_TRUE;
    samplerCreateInfo.maxAnisotropy = 16.0f;
    samplerCreateInfo.compareEnable = VK_FALSE;
    samplerCreateInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerCreateInfo.mipLodBias = 0.0f;
    samplerCreateInfo.minLod = 0.0f;
    samplerCreateInfo.maxLod = 0.0f;
    VkSampler sampler;
    if (vkCreateSampler(m_device, &samplerCreateInfo, nullptr, &sampler) != VK_SUCCESS) {
        LogError("Could not create a sampler for the image.\n");
    }

    TextureInfo textureInfo {};
    textureInfo.image = image;
    textureInfo.memory = deviceMemory;
    textureInfo.format = format;
    textureInfo.view = imageView;
    textureInfo.sampler = sampler;

    // TODO: Use free lists!
    size_t index = m_textureInfos.size();
    m_textureInfos.push_back(textureInfo);
    texture.registerBackend(backendBadge(), index);
}

void VulkanBackend::deleteTexture(const Texture2D& texture)
{
    if (texture.id() == Resource::NullId) {
        LogErrorAndExit("Trying to delete an already-deleted or not-yet-created texture\n");
    }

    // TODO: When we have a free list, also maybe remove from the m_bufferInfos vector? But then we should also keep track of generations etc.
    TextureInfo& textureInfo = m_textureInfos[texture.id()];

    vkDestroySampler(m_device, textureInfo.sampler, nullptr);
    vkDestroyImageView(m_device, textureInfo.view, nullptr);
    vkDestroyImage(m_device, textureInfo.image, nullptr);
    if (textureInfo.memory.has_value()) {
        vkFreeMemory(m_device, textureInfo.memory.value(), nullptr);
    }

    texture.unregisterBackend(backendBadge());
}

void VulkanBackend::updateTexture(const TextureUpdateFromFile& update)
{
    if (update.texture().id() == Resource::NullId) {
        LogErrorAndExit("Trying to update an already-deleted or not-yet-created texture\n");
    }

    if (!fileio::isFileReadable(update.path())) {
        LogError("VulkanBackend::updateTexture(): there is no file that can be read at path '%s'.\n", update.path().c_str());
        return;
    }

    // TODO: Well, if the texture isn't a float texture
    ASSERT(!stbi_is_hdr(update.path().c_str()));

    int width, height, numChannels; // FIXME: Check the number of channels instead of forcing RGBA
    stbi_uc* pixels = stbi_load(update.path().c_str(), &width, &height, &numChannels, STBI_rgb_alpha);
    if (!pixels) {
        LogError("VulkanBackend::updateTexture(): stb_image could not read the contents of '%s'.\n", update.path().c_str());
        stbi_image_free(pixels);
        return;
    }

    if (width != update.texture().extent().width() || height != update.texture().extent().height()) {
        LogErrorAndExit("VulkanBackend::updateTexture(): loaded texture does not match specified extent.\n");
    }

    VkDeviceSize pixelsSize = width * height * numChannels * sizeof(stbi_uc);
    VkDeviceMemory stagingMemory;
    VkBuffer stagingBuffer = createBuffer(pixelsSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingMemory);
    if (!setBufferMemoryDirectly(stagingMemory, pixels, pixelsSize)) {
        LogError("VulkanBackend::updateTexture(): could not set the staging buffer memory.\n");
    }
    AT_SCOPE_EXIT([&]() {
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingMemory, nullptr);
        stbi_image_free(pixels);
    });

    TextureInfo& textureInfo = m_textureInfos[update.texture().id()];

    if (!transitionImageLayout(textureInfo.image, textureInfo.format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)) {
        LogError("VulkanBackend::updateTexture(): could not transition the image to transfer layout.\n");
    }
    if (!copyBufferToImage(stagingBuffer, textureInfo.image, width, height)) {
        LogError("VulkanBackend::updateTexture(): could not copy the staging buffer to the image.\n");
    }

    // TODO: We probably don't wanna use VK_IMAGE_LAYOUT_GENERAL here!
    if (!transitionImageLayout(textureInfo.image, textureInfo.format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL)) {
        LogError("VulkanBackend::updateTexture(): could not transition the image to general layout.\n");
    }
    //if (!transitionImageLayout(textureInfo.image, textureInfo.format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
    //    LogError("VulkanBackend::updateTexture(): could not transition the image to shader-read-only layout.\n");
    //}
}

VkImage VulkanBackend::image(const Texture2D& texture)
{
    TextureInfo& imageInfo = m_textureInfos[texture.id()];
    return imageInfo.image;
}

void VulkanBackend::newRenderTarget(const RenderTarget& renderTarget)
{
    std::vector<VkImageView> allAttachmentImageViews {};
    std::vector<VkAttachmentDescription> allAttachments {};
    std::vector<VkAttachmentReference> colorAttachmentRefs {};
    std::optional<VkAttachmentReference> depthAttachmentRef {};

    for (auto& [type, texture] : renderTarget.sortedAttachments()) {

        // If the attachments are sorted properly (i.e. depth very last) then this should never happen!
        // This is important for the VkAttachmentReference attachment index later in this loop.
        ASSERT(!depthAttachmentRef.has_value());

        TextureInfo& textureInfo = m_textureInfos[texture.id()];

        // TODO: Handle multisampling, clearing, storing, and stencil stuff!
        VkAttachmentDescription attachment = {};
        attachment.format = textureInfo.format;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

        VkImageLayout finalLayout;
        if (type == RenderTarget::AttachmentType::Depth) {
            finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        } else {
            finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }

        // TODO: What should this be in our case? Do we need to keep track of what happened to the image before this pass?
        //  If so, we might stuff that information in the TextureInfo struct for the specific texture. But I think this works?
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = finalLayout;

        uint32_t attachmentIndex = allAttachments.size();
        allAttachments.push_back(attachment);
        allAttachmentImageViews.push_back(textureInfo.view);

        if (type == RenderTarget::AttachmentType::Depth) {
            VkAttachmentReference attachmentRef = {};
            attachmentRef.attachment = attachmentIndex;
            attachmentRef.layout = finalLayout;
            depthAttachmentRef = attachmentRef;
        } else {
            VkAttachmentReference attachmentRef = {};
            attachmentRef.attachment = attachmentIndex;
            attachmentRef.layout = finalLayout;
            colorAttachmentRefs.push_back(attachmentRef);
        }
    }

    // TODO: How do we want to support multiple subpasses in the future?
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = colorAttachmentRefs.size();
    subpass.pColorAttachments = colorAttachmentRefs.data();
    if (depthAttachmentRef.has_value()) {
        subpass.pDepthStencilAttachment = &depthAttachmentRef.value();
    }

    VkRenderPassCreateInfo renderPassCreateInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    renderPassCreateInfo.attachmentCount = allAttachments.size();
    renderPassCreateInfo.pAttachments = allAttachments.data();
    renderPassCreateInfo.subpassCount = 1;
    renderPassCreateInfo.pSubpasses = &subpass;
    renderPassCreateInfo.dependencyCount = 0;
    renderPassCreateInfo.pDependencies = nullptr;

    VkRenderPass renderPass {};
    if (vkCreateRenderPass(m_device, &renderPassCreateInfo, nullptr, &renderPass) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create render pass\n");
    }

    VkFramebufferCreateInfo framebufferCreateInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    framebufferCreateInfo.renderPass = renderPass;
    framebufferCreateInfo.attachmentCount = allAttachmentImageViews.size();
    framebufferCreateInfo.pAttachments = allAttachmentImageViews.data();
    framebufferCreateInfo.width = renderTarget.extent().width();
    framebufferCreateInfo.height = renderTarget.extent().height();
    framebufferCreateInfo.layers = 1;

    VkFramebuffer framebuffer;
    if (vkCreateFramebuffer(m_device, &framebufferCreateInfo, nullptr, &framebuffer) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create framebuffer\n");
    }

    RenderTargetInfo renderTargetInfo {};
    renderTargetInfo.compatibleRenderPass = renderPass;
    renderTargetInfo.framebuffer = framebuffer;

    // TODO: Use free lists!
    size_t index = m_renderTargetInfos.size();
    m_renderTargetInfos.push_back(renderTargetInfo);
    renderTarget.registerBackend(backendBadge(), index);
}

void VulkanBackend::deleteRenderTarget(const RenderTarget& renderTarget)
{
    if (renderTarget.id() == Resource::NullId) {
        LogErrorAndExit("Trying to delete an already-deleted or not-yet-created render target\n");
    }

    // TODO: When we have a free list, also maybe remove from the m_renderTargetInfos vector? But then we should also keep track of generations etc.
    RenderTargetInfo& renderTargetInfo = m_renderTargetInfos[renderTarget.id()];
    vkDestroyFramebuffer(m_device, renderTargetInfo.framebuffer, nullptr);
    vkDestroyRenderPass(m_device, renderTargetInfo.compatibleRenderPass, nullptr);

    renderTarget.unregisterBackend(backendBadge());
}

void VulkanBackend::setupWindowRenderTargets()
{
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = m_swapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment = {};
    depthAttachment.format = m_depthImageFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    std::array<VkAttachmentDescription, 2> allAttachments = {
        colorAttachment,
        depthAttachment
    };

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef = {};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    // Setup subpass dependency to make sure we have the right stuff before drawing to a swapchain image.
    // see https://vulkan-tutorial.com/en/Drawing_a_triangle/Drawing/Rendering_and_presentation for info.
    VkSubpassDependency subpassDependency = {};
    subpassDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    subpassDependency.dstSubpass = 0; // i.e. the first and only subpass we have here
    subpassDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDependency.srcAccessMask = 0;
    subpassDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassCreateInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    renderPassCreateInfo.attachmentCount = allAttachments.size();
    renderPassCreateInfo.pAttachments = allAttachments.data();
    renderPassCreateInfo.subpassCount = 1;
    renderPassCreateInfo.pSubpasses = &subpass;
    renderPassCreateInfo.dependencyCount = 1;
    renderPassCreateInfo.pDependencies = &subpassDependency;

    VkRenderPass renderPass {};
    if (vkCreateRenderPass(m_device, &renderPassCreateInfo, nullptr, &renderPass) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create window render pass\n");
    }

    m_windowRenderTargetInfos.resize(m_numSwapchainImages);
    for (size_t it = 0; it < m_numSwapchainImages; ++it) {

        std::array<VkImageView, 2> attachmentImageViews = {
            m_swapchainImageViews[it],
            m_depthImageView
        };

        VkFramebufferCreateInfo framebufferCreateInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        framebufferCreateInfo.renderPass = renderPass;
        framebufferCreateInfo.attachmentCount = attachmentImageViews.size();
        framebufferCreateInfo.pAttachments = attachmentImageViews.data();
        framebufferCreateInfo.width = m_swapchainExtent.width();
        framebufferCreateInfo.height = m_swapchainExtent.height();
        framebufferCreateInfo.layers = 1;

        VkFramebuffer framebuffer;
        if (vkCreateFramebuffer(m_device, &framebufferCreateInfo, nullptr, &framebuffer) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to create window framebuffer\n");
        }

        m_windowRenderTargetInfos[it].compatibleRenderPass = renderPass;
        m_windowRenderTargetInfos[it].framebuffer = framebuffer;
    }
}

void VulkanBackend::destroyWindowRenderTargets()
{
    for (RenderTargetInfo& renderTargetInfo : m_windowRenderTargetInfos) {
        vkDestroyFramebuffer(m_device, renderTargetInfo.framebuffer, nullptr);
    }

    VkRenderPass sharedRenderPass = m_windowRenderTargetInfos.front().compatibleRenderPass;
    vkDestroyRenderPass(m_device, sharedRenderPass, nullptr);
}

void VulkanBackend::newRenderPass(const RenderPass& renderPass)
{
    // TODO: Create stuff according to specs and store away etc..
    renderPass.registerBackend(backendBadge(), 789);
}

const VulkanBackend::RenderPassInfo& VulkanBackend::renderPassInfo(const RenderPass& renderPass)
{
    return m_renderPassInfos[renderPass.id()];
}

void VulkanBackend::reconstructPipeline(GpuPipeline& pipeline, const ApplicationState& appState)
{
    // TODO: Implement some kind of smart resource diff where we only delete and create resources that actually change.

    m_frameResourceManagers.resize(m_numSwapchainImages);
    for (size_t i = 0; i < m_numSwapchainImages; ++i) {

        auto resourceManager = std::make_unique<ResourceManager>(i);
        pipeline.constructAll(*resourceManager);

        const ResourceManager& previousManager = *m_frameResourceManagers[i];

        // Delete old resources
        for (auto& buffer : previousManager.buffers()) {
            deleteBuffer(buffer);
        }
        for (auto& texture : previousManager.textures()) {
            deleteTexture(texture);
        }
        for (auto& renderTarget : previousManager.renderTargets()) {
            deleteRenderTarget(renderTarget);
        }

        // Create new resources
        for (auto& buffer : resourceManager->buffers()) {
            newBuffer(buffer);
        }
        for (auto& texture : resourceManager->textures()) {
            newTexture(texture);
        }
        for (auto& renderTarget : resourceManager->renderTargets()) {
            newRenderTarget(renderTarget);
        }

        // Perform the instant actions
        for (auto& bufferUpdate : resourceManager->bufferUpdates()) {
            updateBuffer(bufferUpdate);
        }
        for (auto& textureUpdate : resourceManager->textureUpdates()) {
            updateTexture(textureUpdate);
        }

        // Replace previous resource manager
        m_frameResourceManagers[i] = std::move(resourceManager);
    }
}

void VulkanBackend::timeStepForFrame(uint32_t relFrameIndex, double elapsedTime, double deltaTime)
{
    // FIXME: this is a bit sketchy.. unhandled is not the same as 'definitely this frame'
    bool windowSizeDidChange = m_unhandledWindowResize;
    ApplicationState appState { m_swapchainExtent, windowSizeDidChange, deltaTime, elapsedTime, relFrameIndex };

    if (m_gpuPipeline.needsReconstruction(appState)) {
        reconstructPipeline(m_gpuPipeline, appState);
    }

    //auto passes = app().dependencyResolvedPasses();

    // TODO: How about we don't have any of these needsUpdate pass, and instead just figures out
    //  if there are new commands, which implies that there are updates needed.

    // TODO: However, it could be tricky if we don't know if command buffers need rerecording util after we have called execute(), maybe?
    /*
    for (const RenderPass& pass : app().passTree()) {
        RenderPass::CommandList commandList {};

        // NOTE: This function both fills out the command list and performs updates etc. to its own resources
        pass.execute(appState, commandList);

        if (commandList != previousCommandList ) { // maybeJustForThisRelFrameIndex? Or maybe globally new command list?
            // todo: now we actually have to rerecord the command buffers!
        }
    }
    */

    // Update the uniform buffer(s)
    CameraState cameraState = {};
    cameraState.world_from_local = mathkit::axisAngle({ 0, 1, 0 }, elapsedTime * 3.1415f / 2.0f);
    cameraState.view_from_world = mathkit::lookAt({ 0, 1, 2 }, { 0, 0, 0 });
    float aspectRatio = float(m_swapchainExtent.width()) / float(m_swapchainExtent.height());
    cameraState.projection_from_view = mathkit::infinitePerspective(mathkit::radians(45), aspectRatio, 0.1f);

    cameraState.view_from_local = cameraState.view_from_world * cameraState.world_from_local;
    cameraState.projection_from_local = cameraState.projection_from_view * cameraState.view_from_local;

    if (!setBufferMemoryDirectly(m_exCameraStateBufferMemories[relFrameIndex], &cameraState, sizeof(CameraState))) {
        LogError("VulkanBackend::timeStepForFrame(): could not update the uniform buffer.\n");
    }
}

bool VulkanBackend::issueSingleTimeCommand(const std::function<void(VkCommandBuffer)>& callback) const
{
    VkCommandBufferAllocateInfo commandBufferAllocInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    commandBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocInfo.commandPool = m_transientCommandPool;
    commandBufferAllocInfo.commandBufferCount = 1;

    VkCommandBuffer oneTimeCommandBuffer;
    vkAllocateCommandBuffers(m_device, &commandBufferAllocInfo, &oneTimeCommandBuffer);
    AT_SCOPE_EXIT([&] {
        vkFreeCommandBuffers(m_device, m_transientCommandPool, 1, &oneTimeCommandBuffer);
    });

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(oneTimeCommandBuffer, &beginInfo) != VK_SUCCESS) {
        LogError("VulkanBackend::issueSingleTimeCommand(): could not begin the command buffer.\n");
        return false;
    }

    callback(oneTimeCommandBuffer);

    if (vkEndCommandBuffer(oneTimeCommandBuffer) != VK_SUCCESS) {
        LogError("VulkanBackend::issueSingleTimeCommand(): could not end the command buffer.\n");
        return false;
    }

    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &oneTimeCommandBuffer;

    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        LogError("VulkanBackend::issueSingleTimeCommand(): could not submit the single-time command buffer.\n");
        return false;
    }
    if (vkQueueWaitIdle(m_graphicsQueue) != VK_SUCCESS) {
        LogError("VulkanBackend::issueSingleTimeCommand(): error while waiting for the graphics queue to idle.\n");
        return false;
    }

    return true;
}

VkBuffer VulkanBackend::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryProperties, VkDeviceMemory& memory)
{
    VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferCreateInfo.size = size;
    bufferCreateInfo.usage = usage;

    VkBuffer buffer;
    if (vkCreateBuffer(m_device, &bufferCreateInfo, nullptr, &buffer) != VK_SUCCESS) {
        LogError("VulkanBackend::createBuffer(): could not create buffer of size %u.\n", size);
        memory = {};
        return {};
    }

    VkMemoryRequirements memoryRequirements;
    vkGetBufferMemoryRequirements(m_device, buffer, &memoryRequirements);

    VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.memoryTypeIndex = findAppropriateMemory(memoryRequirements.memoryTypeBits, memoryProperties);
    allocInfo.allocationSize = memoryRequirements.size;

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        LogError("VulkanBackend::createBuffer(): could not allocate the required memory of size %u.\n", size);
        memory = {};
        return {};
    }

    if (vkBindBufferMemory(m_device, buffer, memory, 0) != VK_SUCCESS) {
        LogError("VulkanBackend::createBuffer(): could not bind the allocated memory to the buffer.\n");
        return {};
    }

    return buffer;
}

bool VulkanBackend::copyBuffer(VkBuffer source, VkBuffer destination, VkDeviceSize size) const
{
    VkBufferCopy bufferCopyRegion = {};
    bufferCopyRegion.size = size;
    bufferCopyRegion.srcOffset = 0;
    bufferCopyRegion.dstOffset = 0;

    bool success = issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
        vkCmdCopyBuffer(commandBuffer, source, destination, 1, &bufferCopyRegion);
    });

    if (!success) {
        LogError("VulkanBackend::copyBuffer(): error copying buffer, refer to issueSingleTimeCommand errors for more information.\n");
        return false;
    }

    return true;
}

bool VulkanBackend::setBufferMemoryDirectly(VkDeviceMemory memory, const void* data, VkDeviceSize size, VkDeviceSize offset)
{
    void* sharedMemory;
    if (vkMapMemory(m_device, memory, offset, size, 0u, &sharedMemory) != VK_SUCCESS) {
        LogError("VulkanBackend::setBufferMemoryDirectly(): could not map the memory for loading data.\n");
        return false;
    }
    std::memcpy(sharedMemory, data, size);
    vkUnmapMemory(m_device, memory);

    return true;
}

bool VulkanBackend::setBufferDataUsingStagingBuffer(VkBuffer buffer, const void* data, VkDeviceSize size, VkDeviceSize offset)
{
    VkDeviceMemory stagingMemory;
    VkBuffer stagingBuffer = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingMemory);

    AT_SCOPE_EXIT([&] {
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingMemory, nullptr);
    });

    if (!setBufferMemoryDirectly(stagingMemory, data, size)) {
        LogError("VulkanBackend::setBufferDataUsingStagingBuffer(): could not set staging data.\n");
        return false;
    }

    if (!copyBuffer(stagingBuffer, buffer, size)) {
        LogError("VulkanBackend::setBufferDataUsingStagingBuffer(): could not copy from staging buffer to buffer.\n");
        return false;
    }

    return true;
}

VkBuffer VulkanBackend::createDeviceLocalBuffer(VkDeviceSize size, const void* data, VkBufferUsageFlags usage)
{
    // Make sure that we can transfer to this buffer
    usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VkDeviceMemory memory;
    VkBuffer buffer = createBuffer(size, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memory);

    if (!setBufferDataUsingStagingBuffer(buffer, data, size)) {
        LogError("VulkanBackend::createDeviceLocalBuffer(): could not set data through a staging buffer.\n");
    }

    // FIXME: This is only temporary! Later we should keep some shared memory which with offset buffers etc..
    m_managedBuffers.push_back({ buffer, memory });

    return buffer;
}

VkImage VulkanBackend::createImage2D(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkMemoryPropertyFlags memoryProperties, VkDeviceMemory& memory, VkImageTiling tiling)
{
    VkImageCreateInfo imageCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.extent = { .width = width, .height = height, .depth = 1 };
    imageCreateInfo.mipLevels = 1; // FIXME?
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.usage = usage;
    imageCreateInfo.format = format;
    imageCreateInfo.tiling = tiling;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkImage image;
    if (vkCreateImage(m_device, &imageCreateInfo, nullptr, &image) != VK_SUCCESS) {
        LogError("VulkanBackend::createImage2D(): could not create image.\n");
        memory = {};
        return {};
    }

    VkMemoryRequirements memoryRequirements;
    vkGetImageMemoryRequirements(m_device, image, &memoryRequirements);

    VkMemoryAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocateInfo.memoryTypeIndex = findAppropriateMemory(memoryRequirements.memoryTypeBits, memoryProperties);
    allocateInfo.allocationSize = memoryRequirements.size;

    if (vkAllocateMemory(m_device, &allocateInfo, nullptr, &memory) != VK_SUCCESS) {
        LogError("VulkanBackend::createImage2D(): could not allocate memory for image.\n");
        memory = {};
        return {};
    }

    if (vkBindImageMemory(m_device, image, memory, 0) != VK_SUCCESS) {
        LogError("VulkanBackend::createImage2D(): could not bind the allocated memory to the image.\n");
        return {};
    }

    return image;
}

VkImageView VulkanBackend::createImageView2D(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) const
{
    VkImageViewCreateInfo viewCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };

    viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewCreateInfo.subresourceRange.aspectMask = aspectFlags;
    viewCreateInfo.image = image;
    viewCreateInfo.format = format;

    viewCreateInfo.components = {
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY
    };

    viewCreateInfo.subresourceRange.baseMipLevel = 0;
    viewCreateInfo.subresourceRange.levelCount = 1;
    viewCreateInfo.subresourceRange.baseArrayLayer = 0;
    viewCreateInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(m_device, &viewCreateInfo, nullptr, &imageView) != VK_SUCCESS) {
        LogError("VulkanBackend::createImageView2D(): could not create the image view.\n");
    }

    return imageView;
}

bool VulkanBackend::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) const
{
    VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    imageBarrier.oldLayout = oldLayout;
    imageBarrier.newLayout = newLayout;
    imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    imageBarrier.image = image;
    imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageBarrier.subresourceRange.baseMipLevel = 0;
    imageBarrier.subresourceRange.levelCount = 1;
    imageBarrier.subresourceRange.baseArrayLayer = 0;
    imageBarrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        imageBarrier.srcAccessMask = 0;

        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    } else {
        LogErrorAndExit("VulkanBackend::transitionImageLayout(): old & new layout combination unsupported by application, exiting.\n");
    }

    bool success = issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
        vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0,
            0, nullptr,
            0, nullptr,
            1, &imageBarrier);
    });

    if (!success) {
        LogError("VulkanBackend::transitionImageLayout(): error transitioning layout, refer to issueSingleTimeCommand errors for more information.\n");
        return false;
    }

    return true;
}

bool VulkanBackend::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const
{
    VkBufferImageCopy region = {};
    region.bufferOffset = 0;

    // (zeros here indicate tightly packed data)
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageOffset = VkOffset3D { 0, 0, 0 };
    region.imageExtent = VkExtent3D { width, height, 1 };

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    bool success = issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
        // NOTE: This assumes that the image we are copying to has the VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL layout!
        vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    });

    if (!success) {
        LogError("VulkanBackend::copyBufferToImage(): error copying buffer to image, refer to issueSingleTimeCommand errors for more information.\n");
        return false;
    }

    return true;
}

VulkanBackend::ManagedImage VulkanBackend::createImageViewFromImagePath(const std::string& imagePath)
{
    if (!fileio::isFileReadable(imagePath)) {
        LogError("VulkanBackend::createImageFromImage(): there is no file that can be read at path '%s'.\n", imagePath.c_str());
        return {};
    }

    ASSERT(!stbi_is_hdr(imagePath.c_str()));

    int width, height, numChannels; // FIXME: Check the number of channels instead of forcing RGBA
    stbi_uc* pixels = stbi_load(imagePath.c_str(), &width, &height, &numChannels, STBI_rgb_alpha);
    if (!pixels) {
        LogError("VulkanBackend::createImageFromImage(): stb_image could not read the contents of '%s'.\n", imagePath.c_str());
        stbi_image_free(pixels);
        return {};
    }

    VkDeviceSize imageSize = width * height * numChannels * sizeof(stbi_uc);
    VkFormat imageFormat = VK_FORMAT_B8G8R8A8_UNORM; // TODO: Use sRGB images for this type of stuff!

    VkDeviceMemory stagingMemory;
    VkBuffer stagingBuffer = createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingMemory);
    if (!setBufferMemoryDirectly(stagingMemory, pixels, imageSize)) {
        LogError("VulkanBackend::createImageFromImagePath(): could not set the staging buffer memory.\n");
    }
    AT_SCOPE_EXIT([&]() {
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingMemory, nullptr);
        stbi_image_free(pixels);
    });

    VkDeviceMemory imageMemory;
    VkImage image = createImage2D(width, height, imageFormat, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, imageMemory);

    if (!transitionImageLayout(image, imageFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)) {
        LogError("VulkanBackend::createImageFromImagePath(): could not transition the image to transfer layout.\n");
    }
    if (!copyBufferToImage(stagingBuffer, image, width, height)) {
        LogError("VulkanBackend::createImageFromImagePath(): could not copy the staging buffer to the image.\n");
    }
    if (!transitionImageLayout(image, imageFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
        LogError("VulkanBackend::createImageFromImagePath(): could not transition the image to shader-read-only layout.\n");
    }

    VkImageView imageView = createImageView2D(image, imageFormat, VK_IMAGE_ASPECT_COLOR_BIT);

    VkSamplerCreateInfo samplerCreateInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;
    samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
    samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
    samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerCreateInfo.anisotropyEnable = VK_TRUE;
    samplerCreateInfo.maxAnisotropy = 16.0f;
    samplerCreateInfo.compareEnable = VK_FALSE;
    samplerCreateInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerCreateInfo.mipLodBias = 0.0f;
    samplerCreateInfo.minLod = 0.0f;
    samplerCreateInfo.maxLod = 0.0f;

    VkSampler sampler;
    if (vkCreateSampler(m_device, &samplerCreateInfo, nullptr, &sampler) != VK_SUCCESS) {
        LogError("VulkanBackend::createImageFromImagePath(): could not create the sampler for the image.\n");
    }

    ManagedImage managedImage = { sampler, imageView, image, imageMemory };

    // FIXME: This is only temporary! Later we should keep some shared memory which with offset buffers etc..
    m_managedImages.push_back(managedImage);

    return managedImage;
}

void VulkanBackend::createTheDrawingStuff(VkFormat finalTargetFormat, VkExtent2D finalTargetExtent, const std::vector<VkImageView>& swapchainImageViews, VkImageView depthImageView, VkFormat depthFormat)
{
    size_t numSwapchainImages = swapchainImageViews.size();

    ManagedImage testImage = createImageViewFromImagePath("assets/test-pattern.png");

    struct ExampleVertex {
        vec3 position;
        vec3 color;
        vec2 texCoord;
    };

    //
    // Create shader modules (to be removed before the end of this function!)
    //
    VkShaderModule vertShaderModule;
    {
        auto optionalData = fileio::readEntireFileAsByteBuffer("shaders/example.vert.spv");
        ASSERT(optionalData.has_value());
        const auto& binaryData = optionalData.value();

        VkShaderModuleCreateInfo moduleCreateInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        moduleCreateInfo.codeSize = binaryData.size();
        moduleCreateInfo.pCode = reinterpret_cast<const uint32_t*>(binaryData.data());
        ASSERT(vkCreateShaderModule(m_device, &moduleCreateInfo, nullptr, &vertShaderModule) == VK_SUCCESS);
    }

    VkShaderModule fragShaderModule;
    {
        auto optionalData = fileio::readEntireFileAsByteBuffer("shaders/example.frag.spv");
        ASSERT(optionalData.has_value());
        const auto& binaryData = optionalData.value();

        VkShaderModuleCreateInfo moduleCreateInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        moduleCreateInfo.codeSize = binaryData.size();
        moduleCreateInfo.pCode = reinterpret_cast<const uint32_t*>(binaryData.data());
        ASSERT(vkCreateShaderModule(m_device, &moduleCreateInfo, nullptr, &fragShaderModule) == VK_SUCCESS);
    }

    AT_SCOPE_EXIT([&]() {
        vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
        vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
    })

    //
    // Create uniform buffers
    //
    m_exCameraStateBuffers.resize(numSwapchainImages);
    m_exCameraStateBufferMemories.resize(numSwapchainImages);
    for (size_t it = 0; it < numSwapchainImages; ++it) {

        VkDeviceMemory memory;
        VkBuffer buffer = createBuffer(sizeof(CameraState), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memory);

        m_exCameraStateBuffers[it] = buffer;
        m_exCameraStateBufferMemories[it] = memory;

        // TODO: Don't manage the memory like this!
        m_managedBuffers.push_back({ buffer, memory });
    }

    //
    // Create descriptor set layout
    //
    {
        VkDescriptorSetLayoutBinding cameraStateUboLayoutBinding = {};
        cameraStateUboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        cameraStateUboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        cameraStateUboLayoutBinding.binding = 0;
        cameraStateUboLayoutBinding.descriptorCount = 1;
        cameraStateUboLayoutBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
        samplerLayoutBinding.binding = 1;
        samplerLayoutBinding.descriptorCount = 1;
        samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerLayoutBinding.pImmutableSamplers = nullptr;

        std::array<VkDescriptorSetLayoutBinding, 2> allBindings = { cameraStateUboLayoutBinding, samplerLayoutBinding };

        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        descriptorSetLayoutCreateInfo.bindingCount = allBindings.size();
        descriptorSetLayoutCreateInfo.pBindings = allBindings.data();
        ASSERT(vkCreateDescriptorSetLayout(m_device, &descriptorSetLayoutCreateInfo, nullptr, &m_exDescriptorSetLayout) == VK_SUCCESS);
    }

    {
        //
        // Create pipeline layout
        //
        VkVertexInputBindingDescription bindingDescription = {};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(ExampleVertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions {};

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(ExampleVertex, position);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(ExampleVertex, color);

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(ExampleVertex, texCoord);

        VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        pipelineLayoutCreateInfo.setLayoutCount = 1;
        pipelineLayoutCreateInfo.pSetLayouts = &m_exDescriptorSetLayout;
        pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
        ASSERT(vkCreatePipelineLayout(m_device, &pipelineLayoutCreateInfo, nullptr, &m_exPipelineLayout) == VK_SUCCESS);

        //
        // Create pipeline
        //
        VkPipelineVertexInputStateCreateInfo vertInputState = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vertInputState.vertexBindingDescriptionCount = 1;
        vertInputState.pVertexBindingDescriptions = &bindingDescription;
        vertInputState.vertexAttributeDescriptionCount = attributeDescriptions.size();
        vertInputState.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssemblyState.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(finalTargetExtent.width);
        viewport.height = static_cast<float>(finalTargetExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor = {};
        scissor.offset = { 0, 0 };
        scissor.extent = finalTargetExtent;

        VkPipelineViewportStateCreateInfo viewportState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        // TODO: Consider what we want here!
        /*
        VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_LINE_WIDTH
        };
        VkPipelineDynamicStateCreateInfo dynamicState = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;
        */

        VkPipelineShaderStageCreateInfo vertStageCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        vertStageCreateInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStageCreateInfo.module = vertShaderModule;
        vertStageCreateInfo.pName = "main";

        VkPipelineShaderStageCreateInfo fragStageCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        fragStageCreateInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStageCreateInfo.module = fragShaderModule;
        fragStageCreateInfo.pName = "main";

        VkPipelineDepthStencilStateCreateInfo depthStencilState = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        depthStencilState.depthTestEnable = VK_TRUE;
        depthStencilState.depthWriteEnable = VK_TRUE;
        depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencilState.depthBoundsTestEnable = VK_FALSE;
        depthStencilState.minDepthBounds = 0.0f;
        depthStencilState.maxDepthBounds = 1.0f;
        depthStencilState.stencilTestEnable = VK_FALSE;
        depthStencilState.front = {};
        depthStencilState.back = {};

        VkGraphicsPipelineCreateInfo pipelineCreateInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        // stages
        VkPipelineShaderStageCreateInfo shaderStages[] = { vertStageCreateInfo, fragStageCreateInfo };
        pipelineCreateInfo.stageCount = 2;
        pipelineCreateInfo.pStages = shaderStages;
        // fixed function stuff
        pipelineCreateInfo.pVertexInputState = &vertInputState;
        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pRasterizationState = &rasterizer;
        pipelineCreateInfo.pMultisampleState = &multisampling;
        pipelineCreateInfo.pDepthStencilState = &depthStencilState;
        pipelineCreateInfo.pColorBlendState = &colorBlending;
        pipelineCreateInfo.pDynamicState = nullptr; //&dynamicState;
        // pipeline layout
        pipelineCreateInfo.layout = m_exPipelineLayout;
        // render pass stuff
        VkRenderPass sharedRenderPass = m_windowRenderTargetInfos.front().compatibleRenderPass;
        pipelineCreateInfo.renderPass = sharedRenderPass;
        pipelineCreateInfo.subpass = 0;
        // extra stuff (optional for this)
        pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
        pipelineCreateInfo.basePipelineIndex = -1;

        ASSERT(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_exGraphicsPipeline) == VK_SUCCESS);
    }

    //
    // Create & update descriptor sets
    //
    {
        std::array<VkDescriptorPoolSize, 2> descriptorPoolSizes {};
        descriptorPoolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorPoolSizes[0].descriptorCount = numSwapchainImages;
        descriptorPoolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorPoolSizes[1].descriptorCount = numSwapchainImages;

        VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        descriptorPoolCreateInfo.poolSizeCount = descriptorPoolSizes.size();
        descriptorPoolCreateInfo.pPoolSizes = descriptorPoolSizes.data();
        descriptorPoolCreateInfo.maxSets = numSwapchainImages;

        ASSERT(vkCreateDescriptorPool(m_device, &descriptorPoolCreateInfo, nullptr, &m_exDescriptorPool) == VK_SUCCESS);

        std::vector<VkDescriptorSetLayout> descriptorSetLayouts { numSwapchainImages, m_exDescriptorSetLayout };
        VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        descriptorSetAllocateInfo.descriptorPool = m_exDescriptorPool;
        descriptorSetAllocateInfo.descriptorSetCount = numSwapchainImages;
        descriptorSetAllocateInfo.pSetLayouts = descriptorSetLayouts.data();

        m_exDescriptorSets.resize(numSwapchainImages);
        ASSERT(vkAllocateDescriptorSets(m_device, &descriptorSetAllocateInfo, m_exDescriptorSets.data()) == VK_SUCCESS);

        for (size_t i = 0; i < numSwapchainImages; ++i) {
            VkDescriptorBufferInfo descriptorBufferInfo = {};
            descriptorBufferInfo.buffer = m_exCameraStateBuffers[i];
            descriptorBufferInfo.range = VK_WHOLE_SIZE; //sizeof(CameraState);
            descriptorBufferInfo.offset = 0;

            VkDescriptorImageInfo descriptorImageInfo = {};
            descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            descriptorImageInfo.imageView = testImage.view;
            descriptorImageInfo.sampler = testImage.sampler;

            VkWriteDescriptorSet uniformBufferWriteDescriptorSet = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            uniformBufferWriteDescriptorSet.dstSet = m_exDescriptorSets[i];
            uniformBufferWriteDescriptorSet.dstBinding = 0;
            uniformBufferWriteDescriptorSet.dstArrayElement = 0;
            uniformBufferWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            uniformBufferWriteDescriptorSet.descriptorCount = 1;
            uniformBufferWriteDescriptorSet.pBufferInfo = &descriptorBufferInfo;
            uniformBufferWriteDescriptorSet.pImageInfo = nullptr;
            uniformBufferWriteDescriptorSet.pTexelBufferView = nullptr;

            VkWriteDescriptorSet imageSamplerWriteDescriptorSet = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            imageSamplerWriteDescriptorSet.dstSet = m_exDescriptorSets[i];
            imageSamplerWriteDescriptorSet.dstBinding = 1;
            imageSamplerWriteDescriptorSet.dstArrayElement = 0;
            imageSamplerWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            imageSamplerWriteDescriptorSet.descriptorCount = 1;
            imageSamplerWriteDescriptorSet.pImageInfo = &descriptorImageInfo;
            imageSamplerWriteDescriptorSet.pBufferInfo = nullptr;
            imageSamplerWriteDescriptorSet.pTexelBufferView = nullptr;

            std::array<VkWriteDescriptorSet, 2> allWriteDescriptorSets = { uniformBufferWriteDescriptorSet, imageSamplerWriteDescriptorSet };
            vkUpdateDescriptorSets(m_device, allWriteDescriptorSets.size(), allWriteDescriptorSets.data(), 0, nullptr);
        }
    }

    //
    // Create command buffers
    //
    m_commandBuffers.resize(numSwapchainImages);
    {
        VkCommandBufferAllocateInfo commandBufferAllocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        commandBufferAllocateInfo.commandPool = m_commandPool;
        commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; // Can be submitted to a queue for execution, but cannot be called from other command buffers.
        commandBufferAllocateInfo.commandBufferCount = m_commandBuffers.size();

        ASSERT(vkAllocateCommandBuffers(m_device, &commandBufferAllocateInfo, m_commandBuffers.data()) == VK_SUCCESS);
    }

    //
    // Write commands to command buffers
    //

    // TODO: The command buffer recording also needs to be redone for the new command buffers,
    // TODO  but I think it seems like a separate step though... Or is it..?

    // TODO: This is the command buffer recording for the example triangle drawing stuff
    std::vector<ExampleVertex> vertices = {
        { vec3(-0.5, -0.5, 0), vec3(1, 0, 0), vec2(1, 0) },
        { vec3(0.5, -0.5, 0), vec3(0, 1, 0), vec2(0, 0) },
        { vec3(0.5, 0.5, 0), vec3(0, 0, 1), vec2(0, 1) },
        { vec3(-0.5, 0.5, 0), vec3(1, 1, 1), vec2(1, 1) },

        { { -0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f } },
        { { 0.5f, -0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } },
        { { 0.5f, 0.5f, -0.5f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
        { { -0.5f, 0.5f, -0.5f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } }
    };
    std::vector<uint16_t> indices = {
        0, 1, 2,
        2, 3, 0,

        4, 5, 6,
        6, 7, 4
    };
    VkBuffer vertexBuffer = createDeviceLocalBuffer(vertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    VkBuffer indexBuffer = createDeviceLocalBuffer(indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    for (size_t it = 0; it < m_commandBuffers.size(); ++it) {

        VkCommandBufferBeginInfo commandBufferBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        commandBufferBeginInfo.flags = 0u;
        commandBufferBeginInfo.pInheritanceInfo = nullptr;

        ASSERT(vkBeginCommandBuffer(m_commandBuffers[it], &commandBufferBeginInfo) == VK_SUCCESS);
        {
            RenderTargetInfo& renderTargetInfo = m_windowRenderTargetInfos[it];

            std::array<VkClearValue, 2> clearValues {};
            clearValues[0].color = { 1.0f, 0.0f, 1.0f, 1.0f };
            clearValues[1].depthStencil = { 1.0f, 0 };

            VkRenderPassBeginInfo renderPassBeginInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
            renderPassBeginInfo.renderPass = renderTargetInfo.compatibleRenderPass;
            renderPassBeginInfo.framebuffer = renderTargetInfo.framebuffer;
            renderPassBeginInfo.renderArea.offset = { 0, 0 };
            renderPassBeginInfo.renderArea.extent = finalTargetExtent;
            renderPassBeginInfo.clearValueCount = clearValues.size();
            renderPassBeginInfo.pClearValues = clearValues.data();

            vkCmdBeginRenderPass(m_commandBuffers[it], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
            {
                vkCmdBindPipeline(m_commandBuffers[it], VK_PIPELINE_BIND_POINT_GRAPHICS, m_exGraphicsPipeline);

                vkCmdBindDescriptorSets(m_commandBuffers[it], VK_PIPELINE_BIND_POINT_GRAPHICS, m_exPipelineLayout, 0, 1, &m_exDescriptorSets[it], 0, nullptr);

                VkBuffer vertexBuffers[] = { vertexBuffer };
                VkDeviceSize offsets[] = { 0 };
                vkCmdBindVertexBuffers(m_commandBuffers[it], 0, 1, vertexBuffers, offsets);
                vkCmdBindIndexBuffer(m_commandBuffers[it], indexBuffer, 0, VK_INDEX_TYPE_UINT16);

                vkCmdDrawIndexed(m_commandBuffers[it], indices.size(), 1, 0, 0, 0);
            }
            vkCmdEndRenderPass(m_commandBuffers[it]);
        }
        ASSERT(vkEndCommandBuffer(m_commandBuffers[it]) == VK_SUCCESS);
    }
}

void VulkanBackend::destroyTheDrawingStuff()
{
    vkFreeCommandBuffers(m_device, m_commandPool, m_commandBuffers.size(), m_commandBuffers.data());

    vkDestroyDescriptorPool(m_device, m_exDescriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_exDescriptorSetLayout, nullptr);
    vkDestroyPipeline(m_device, m_exGraphicsPipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_exPipelineLayout, nullptr);
}

void VulkanBackend::submitQueue(uint32_t imageIndex, VkSemaphore* waitFor, VkSemaphore* signal, VkFence* inFlight)
{
    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitFor;
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffers[imageIndex];

    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signal;

    if (vkResetFences(m_device, 1, inFlight) != VK_SUCCESS) {
        LogError("VulkanBackend::submitQueue(): error resetting in-flight frame fence (index %u).\n", imageIndex);
    }

    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, *inFlight) != VK_SUCCESS) {
        LogError("VulkanBackend::submitQueue(): could not submit the graphics queue (index %u).\n", imageIndex);
    }
}

uint32_t VulkanBackend::findAppropriateMemory(uint32_t typeBits, VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memoryProperties);

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++) {
        // Is type i at all supported, given the typeBits?
        if (!(typeBits & (1u << i))) {
            continue;
        }

        if ((memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    LogErrorAndExit("VulkanBackend::findAppropriateMemory(): could not find any appropriate memory, exiting.\n");
}
