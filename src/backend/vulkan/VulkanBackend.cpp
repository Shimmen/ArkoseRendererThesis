#include "VulkanBackend.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "VulkanQueueInfo.h"
#include "rendering/ShaderManager.h"
#include "utility/fileio.h"
#include "utility/logging.h"
#include "utility/util.h"
#include <algorithm>
#include <cstring>
#include <stb_image.h>
#include <unordered_map>
#include <unordered_set>

VulkanBackend::VulkanBackend(GLFWwindow* window, App& app)
    : m_window(window)
    , m_app(app)
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
    poolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // (so we can easily reuse them each frame)
    if (vkCreateCommandPool(m_device, &poolCreateInfo, nullptr, &m_renderGraphFrameCommandPool) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend::VulkanBackend(): could not create command pool for the graphics queue, exiting.\n");
    }

    VkCommandPoolCreateInfo transientPoolCreateInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    transientPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    transientPoolCreateInfo.queueFamilyIndex = m_queueInfo.graphicsQueueFamilyIndex;
    if (vkCreateCommandPool(m_device, &transientPoolCreateInfo, nullptr, &m_transientCommandPool) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend::VulkanBackend(): could not create transient command pool, exiting.\n");
    }

    createAndSetupSwapchain(m_physicalDevice, m_device, m_surface);

    m_staticResourceManager = std::make_unique<StaticResourceManager>();
    m_app.setup(*m_staticResourceManager);
    createStaticResources();

    m_renderGraph = m_app.mainRenderGraph();
    ApplicationState appState { m_swapchainExtent, 0.0, 0.0, 0 };
    reconstructRenderGraph(*m_renderGraph, appState);

    for (size_t i = 0; i < m_numSwapchainImages; ++i) {
        auto allocator = std::make_unique<FrameAllocator>(frameAllocatorSize);
        m_frameAllocators.push_back(std::move(allocator));
    }
}

VulkanBackend::~VulkanBackend()
{
    // Before destroying stuff, make sure it's done with all scheduled work
    vkDeviceWaitIdle(m_device);

    vkFreeCommandBuffers(m_device, m_renderGraphFrameCommandPool, m_frameCommandBuffers.size(), m_frameCommandBuffers.data());

    destroyRenderGraph(*m_renderGraph);
    destroyStaticResources();

    destroySwapchain();

    vkDestroyCommandPool(m_device, m_renderGraphFrameCommandPool, nullptr);
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

    // Create main command buffers, one per swapchain image
    m_frameCommandBuffers.resize(m_numSwapchainImages);
    {
        VkCommandBufferAllocateInfo commandBufferAllocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        commandBufferAllocateInfo.commandPool = m_renderGraphFrameCommandPool;
        commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; // (can be submitted to a queue for execution, but cannot be called from other command buffers)
        commandBufferAllocateInfo.commandBufferCount = m_frameCommandBuffers.size();

        if (vkAllocateCommandBuffers(m_device, &commandBufferAllocateInfo, m_frameCommandBuffers.data()) != VK_SUCCESS) {
            LogErrorAndExit("VulkanBackend::createAndSetupSwapchain(): could not create the main command buffers, exiting.\n");
        }
    }
}

void VulkanBackend::destroySwapchain()
{
    destroyWindowRenderTargets();

    vkDestroyImageView(m_device, m_depthImageView, nullptr);
    vkDestroyImage(m_device, m_depthImage, nullptr);
    vkFreeMemory(m_device, m_depthImageMemory, nullptr);

    for (size_t it = 0; it < m_numSwapchainImages; ++it) {
        vkDestroyImageView(m_device, m_swapchainImageViews[it], nullptr);
    }

    vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
}

Extent2D VulkanBackend::recreateSwapchain()
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

    return m_swapchainExtent;
}

void VulkanBackend::createStaticResources()
{
    ResourceManager& resourceManager = m_staticResourceManager->internal(backendBadge());

    for (auto& buffer : resourceManager.buffers()) {
        newBuffer(buffer);
    }
    for (auto& bufferUpdate : resourceManager.bufferUpdates()) {
        updateBuffer(bufferUpdate);
    }
    for (auto& texture : resourceManager.textures()) {
        newTexture(texture);
    }
    for (auto& textureUpdate : resourceManager.textureUpdates()) {
        updateTexture(textureUpdate);
    }
}

void VulkanBackend::destroyStaticResources()
{
    ResourceManager& resourceManager = m_staticResourceManager->internal(backendBadge());

    for (auto& buffer : resourceManager.buffers()) {
        deleteBuffer(buffer);
    }
    for (auto& texture : resourceManager.textures()) {
        deleteTexture(texture);
    }
}

bool VulkanBackend::executeFrame(double elapsedTime, double deltaTime)
{
    uint32_t currentFrameMod = m_currentFrameIndex % maxFramesInFlight;

    if (vkWaitForFences(m_device, 1, &m_inFlightFrameFences[currentFrameMod], VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        LogError("VulkanBackend::executeFrame(): error while waiting for in-flight frame fence (frame %u).\n", m_currentFrameIndex);
    }

    ApplicationState appState { m_swapchainExtent, deltaTime, elapsedTime, m_currentFrameIndex };

    uint32_t swapchainImageIndex;
    VkResult acquireResult = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, m_imageAvailableSemaphores[currentFrameMod], VK_NULL_HANDLE, &swapchainImageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        // Since we couldn't acquire an image to draw to, recreate the swapchain and report that it didn't work
        Extent2D newWindowExtent = recreateSwapchain();
        appState = appState.updateWindowExtent(newWindowExtent);
        reconstructRenderGraph(*m_renderGraph, appState);
        return false;
    }
    if (acquireResult == VK_SUBOPTIMAL_KHR) {
        // Since we did manage to acquire an image, just roll with it for now, but it will probably resolve itself after presenting
        LogWarning("VulkanBackend::executeFrame(): next image was acquired but it's suboptimal, ignoring.\n");
    }

    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        LogError("VulkanBackend::executeFrame(): error acquiring next swapchain image.\n");
    }

    ASSERT(m_renderGraph);
    executeRenderGraph(appState, *m_renderGraph, swapchainImageIndex);

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
            Extent2D newWindowExtent = recreateSwapchain();
            appState = appState.updateWindowExtent(newWindowExtent);
            reconstructRenderGraph(*m_renderGraph, appState);
        } else if (presentResult != VK_SUCCESS) {
            LogError("VulkanBackend::executeFrame(): could not present swapchain (frame %u).\n", m_currentFrameIndex);
        }
    }

    m_currentFrameIndex += 1;
    return true;
}

void VulkanBackend::executeRenderGraph(const ApplicationState& appState, const RenderGraph& renderGraph, uint32_t swapchainImageIndex)
{
    FrameAllocator& frameAllocator = *m_frameAllocators[swapchainImageIndex];
    frameAllocator.reset();

    VkCommandBufferBeginInfo commandBufferBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    commandBufferBeginInfo.flags = 0u;
    commandBufferBeginInfo.pInheritanceInfo = nullptr;

    VkCommandBuffer commandBuffer = m_frameCommandBuffers[swapchainImageIndex];
    if (vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo) != VK_SUCCESS) {
        LogError("VulkanBackend::executeRenderGraph(): error beginning command buffer command!\n");
    }

    renderGraph.forEachNodeInResolvedOrder([&](const RenderGraphNode& node) {
        CommandList cmdList {};
        node.execute(appState, cmdList, frameAllocator);

        bool insideRenderPass = false;

        while (cmdList.hasNext()) {

            const auto& command = cmdList.next();

            if (command.is<CmdSetRenderState>()) {
                if (cmdList.hasNext() && cmdList.peekNext().is<CmdClear>()) {
                    executeSetRenderState(commandBuffer, command.as<CmdSetRenderState>(), &cmdList.next().as<CmdClear>(), swapchainImageIndex);
                } else {
                    executeSetRenderState(commandBuffer, command.as<CmdSetRenderState>(), nullptr, swapchainImageIndex);
                }

                insideRenderPass = true;
            }

            else if (command.is<CmdUpdateBuffer>()) {
                auto& cmd = command.as<CmdUpdateBuffer>();
                auto* bytes = static_cast<std::byte*>(cmd.source);
                updateBuffer(cmd.buffer, bytes, cmd.size);
            }

            else if (command.is<CmdClear>()) {
                // For now, just assume it's always after a CmdSetRenderState
                ASSERT_NOT_REACHED();

            }

            else if (command.is<CmdDrawIndexed>()) {
                ASSERT(insideRenderPass);
                executeDrawIndexed(commandBuffer, command.as<CmdDrawIndexed>());
            }

            else {
                LogError("VulkanBackend::executeRenderGraph(): unhandled command!\n");
                ASSERT_NOT_REACHED();
            }
        }

        if (insideRenderPass) {
            vkCmdEndRenderPass(commandBuffer);
            insideRenderPass = false;
        }
    });

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        LogError("VulkanBackend::executeRenderGraph(): error ending command buffer command!\n");
    }
}

void VulkanBackend::executeSetRenderState(VkCommandBuffer commandBuffer, const CmdSetRenderState& cmd, const CmdClear* clearCmd, uint32_t swapchainImageIndex)
{
    const RenderTarget& renderTarget = cmd.renderState.renderTarget();

    // TODO: Now in hindsight it looks like maybe we should have separate renderTarget & pipelineState commands maybe?
    std::vector<VkClearValue> clearValues {};

    if (clearCmd) {
        VkClearColorValue clearColorValue = { { clearCmd->clearColor.r, clearCmd->clearColor.g, clearCmd->clearColor.b, clearCmd->clearColor.a } };
        VkClearDepthStencilValue clearDepthStencilValue = { clearCmd->clearDepth, clearCmd->clearStencil };

        if (renderTarget.isWindowTarget()) {
            clearValues.resize(2);
            clearValues[0].color = clearColorValue;
            clearValues[1].depthStencil = clearDepthStencilValue;
        } else {
            for (auto& [type, _] : renderTarget.sortedAttachments()) {
                VkClearValue value = {};
                if (type == RenderTarget::AttachmentType::Depth) {
                    value.depthStencil = clearDepthStencilValue;
                } else {
                    value.color = clearColorValue;
                }
                clearValues.push_back(value);
            }
        }
    }

    VkRenderPassBeginInfo renderPassBeginInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };

    RenderTargetInfo* targetInfo {};
    if (renderTarget.isWindowTarget()) {
        targetInfo = &m_windowRenderTargetInfos[swapchainImageIndex];
    } else {
        targetInfo = &renderTargetInfo(renderTarget);
    }
    renderPassBeginInfo.renderPass = targetInfo->compatibleRenderPass;
    renderPassBeginInfo.framebuffer = targetInfo->framebuffer;

    auto& targetExtent = renderTarget.isWindowTarget() ? m_swapchainExtent : renderTarget.extent();
    renderPassBeginInfo.renderArea.offset = { 0, 0 };
    renderPassBeginInfo.renderArea.extent = { targetExtent.width(), targetExtent.height() };

    renderPassBeginInfo.clearValueCount = clearValues.size();
    renderPassBeginInfo.pClearValues = clearValues.data();

    // TODO: Handle subpasses properly!
    vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    RenderStateInfo& stateInfo = renderStateInfo(cmd.renderState);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, stateInfo.pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, stateInfo.pipelineLayout, 0, 1, &stateInfo.descriptorSet, 0, nullptr);
}

void VulkanBackend::executeDrawIndexed(VkCommandBuffer commandBuffer, const CmdDrawIndexed& command)
{
    VkBuffer vertexBuffer = buffer(command.vertexBuffer);
    VkBuffer indexBuffer = buffer(command.indexBuffer);

    VkBuffer vertexBuffers[] = { vertexBuffer };
    VkDeviceSize offsets[] = { 0 };

    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT16); // TODO: Index type!
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

    const std::byte* data = update.data().data();
    size_t size = update.data().size();
    updateBuffer(update.buffer(), data, size);
}

void VulkanBackend::updateBuffer(const Buffer& buffer, const std::byte* data, size_t size)
{
    if (buffer.id() == Resource::NullId) {
        LogErrorAndExit("Trying to update an already-deleted or not-yet-created buffer\n");
    }

    BufferInfo& bufferInfo = m_bufferInfos[buffer.id()];

    switch (buffer.memoryHint()) {
    case Buffer::MemoryHint::GpuOptimal:
        setBufferDataUsingStagingBuffer(bufferInfo.buffer, data, size);
        break;
    case Buffer::MemoryHint::TransferOptimal:
        if (!bufferInfo.memory.has_value()) {
            LogErrorAndExit("Trying to update transfer optimal buffer that doesn't own it's memory, which currently isn't unsupported!\n");
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
    textureInfo.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

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
    VkImageLayout finalLayout = VK_IMAGE_LAYOUT_GENERAL;

    if (!transitionImageLayout(textureInfo.image, textureInfo.format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, finalLayout)) {
        LogError("VulkanBackend::updateTexture(): could not transition the image to the specified image layout.\n");
    }
    //if (!transitionImageLayout(textureInfo.image, textureInfo.format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
    //    LogError("VulkanBackend::updateTexture(): could not transition the image to shader-read-only layout.\n");
    //}

    textureInfo.currentLayout = finalLayout;
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

        attachment.initialLayout = textureInfo.currentLayout;
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

VulkanBackend::RenderTargetInfo& VulkanBackend::renderTargetInfo(const RenderTarget& renderTarget)
{
    RenderTargetInfo& renderTargetInfo = m_renderTargetInfos[renderTarget.id()];
    return renderTargetInfo;
}

void VulkanBackend::newRenderState(const RenderState& renderState, uint32_t swapchainImageIndex)
{
    VkVertexInputBindingDescription bindingDescription = {};
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions {};
    {
        const VertexLayout& vertexLayout = renderState.vertexLayout();

        // TODO: What about multiple bindings? Just have multiple VertexLayout:s?
        uint32_t binding = 0;

        bindingDescription.binding = binding;
        bindingDescription.stride = vertexLayout.vertexStride;
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        attributeDescriptions.reserve(vertexLayout.attributes.size());
        for (const VertexAttribute& attribute : vertexLayout.attributes) {

            VkVertexInputAttributeDescription description = {};
            description.binding = binding;
            description.location = attribute.location;
            description.offset = attribute.memoryOffset;

            VkFormat format;
            switch (attribute.type) {
            case VertexAttributeType::Float2:
                format = VK_FORMAT_R32G32_SFLOAT;
                break;
            case VertexAttributeType::Float3:
                format = VK_FORMAT_R32G32B32_SFLOAT;
                break;
            case VertexAttributeType::Float4:
                format = VK_FORMAT_R32G32B32A32_SFLOAT;
                break;
            }
            description.format = format;

            attributeDescriptions.push_back(description);
        }
    }

    std::vector<VkPipelineShaderStageCreateInfo> shaderStages {};
    {
        const Shader& shader = renderState.shader();
        for (auto& file : shader.files()) {

            // TODO: Maybe don't create new modules every time? Currently they are deleted later in this function
            VkShaderModuleCreateInfo moduleCreateInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            const std::vector<uint32_t>& spirv = ShaderManager::instance().spirv(file.name());
            moduleCreateInfo.codeSize = sizeof(uint32_t) * spirv.size();
            moduleCreateInfo.pCode = spirv.data();

            VkShaderModule shaderModule {};
            if (vkCreateShaderModule(m_device, &moduleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS) {
                LogErrorAndExit("Error trying to create shader module\n");
            }

            VkPipelineShaderStageCreateInfo stageCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            stageCreateInfo.module = shaderModule;
            stageCreateInfo.pName = "main";

            VkShaderStageFlagBits stageFlags;
            switch (file.type()) {
            case ShaderFileType::Vertex:
                stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
                break;
            case ShaderFileType::Fragment:
                stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
                break;
            case ShaderFileType::Compute:
                ASSERT_NOT_REACHED();
                break;
            }
            stageCreateInfo.stage = stageFlags;

            shaderStages.push_back(stageCreateInfo);
        }
    }

    //
    // Create descriptor set layout
    //
    VkDescriptorSetLayout descriptorSetLayout {};
    {
        const ShaderBindingSet& bindingSet = renderState.shaderBindingSet();

        std::vector<VkDescriptorSetLayoutBinding> layoutBindings {};
        layoutBindings.reserve(bindingSet.shaderBindings().size());

        for (auto& bindingInfo : bindingSet.shaderBindings()) {

            VkDescriptorSetLayoutBinding binding = {};
            binding.binding = bindingInfo.bindingIndex;

            switch (bindingInfo.type) {
            case ShaderBindingType::UniformBuffer:
                binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                break;
            case ShaderBindingType::TextureSampler:
                binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            }

            // NOTE: Should be 1 unless we have an array, which we currently don't support.
            binding.descriptorCount = 1;

            switch (bindingInfo.shaderFileType) {
            case ShaderFileType::Vertex:
                binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
                break;
            case ShaderFileType::Fragment:
                binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
                break;
            case ShaderFileType::Compute:
                ASSERT_NOT_REACHED();
                break;
            }

            binding.pImmutableSamplers = nullptr;

            layoutBindings.push_back(binding);
        }

        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        descriptorSetLayoutCreateInfo.bindingCount = layoutBindings.size();
        descriptorSetLayoutCreateInfo.pBindings = layoutBindings.data();

        if (vkCreateDescriptorSetLayout(m_device, &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to create descriptor set layout\n");
        }
    }

    //
    // Create pipeline layout
    //
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };

    // TODO: Support multiple descriptor sets!
    pipelineLayoutCreateInfo.setLayoutCount = 1;
    pipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayout;

    // TODO: Support push constants!
    pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
    pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;

    VkPipelineLayout pipelineLayout {};
    if (vkCreatePipelineLayout(m_device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create pipeline layout\n");
    }

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

    auto& viewportInfo = renderState.fixedViewport();

    VkViewport viewport = {};
    viewport.x = viewportInfo.x;
    viewport.y = viewportInfo.y;
    viewport.width = viewportInfo.width;
    viewport.height = viewportInfo.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    // TODO: Should we always use the viewport settings if no scissor is specified?
    VkRect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent.width = uint32_t(viewportInfo.width);
    scissor.extent.height = uint32_t(viewportInfo.height);

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

    // TODO: Implement blending!
    VkPipelineColorBlendStateCreateInfo colorBlending = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    if (renderState.blendState().enabled) {
        ASSERT_NOT_REACHED();
    } else {
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;
    }

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
    pipelineCreateInfo.stageCount = shaderStages.size();
    pipelineCreateInfo.pStages = shaderStages.data();

    // fixed function stuff
    pipelineCreateInfo.pVertexInputState = &vertInputState;
    pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
    pipelineCreateInfo.pViewportState = &viewportState;
    pipelineCreateInfo.pRasterizationState = &rasterizer;
    pipelineCreateInfo.pMultisampleState = &multisampling;
    pipelineCreateInfo.pDepthStencilState = &depthStencilState;
    pipelineCreateInfo.pColorBlendState = &colorBlending;
    pipelineCreateInfo.pDynamicState = nullptr;

    // pipeline layout
    pipelineCreateInfo.layout = pipelineLayout;

    // render pass stuff
    const RenderTarget& renderTarget = renderState.renderTarget();
    const RenderTargetInfo* renderTargetInfo {};
    if (renderTarget.isWindowTarget()) {
        renderTargetInfo = &m_windowRenderTargetInfos[swapchainImageIndex];
    } else {
        renderTargetInfo = &m_renderTargetInfos[renderTarget.id()];
    }
    pipelineCreateInfo.renderPass = renderTargetInfo->compatibleRenderPass;
    pipelineCreateInfo.subpass = 0; // TODO: How should this be handled?

    // extra stuff (optional for this)
    pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineCreateInfo.basePipelineIndex = -1;

    VkPipeline graphicsPipeline {};
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create graphics pipeline\n");
    }

    // Remove shader modules, they are no longer needed after creating the pipeline
    for (auto& stage : shaderStages) {
        vkDestroyShaderModule(m_device, stage.module, nullptr);
    }

    //
    // Create a descriptor pool
    //
    VkDescriptorPool descriptorPool {};
    {
        // TODO: Maybe in the future we don't want one pool per render state? We could group a lot of stuff together probably.

        std::unordered_map<ShaderBindingType, size_t> bindingTypeIndex {};
        std::vector<VkDescriptorPoolSize> descriptorPoolSizes {};

        const ShaderBindingSet& bindingSet = renderState.shaderBindingSet();
        for (auto& bindingInfo : bindingSet.shaderBindings()) {

            ShaderBindingType type = bindingInfo.type;

            auto entry = bindingTypeIndex.find(type);
            if (entry == bindingTypeIndex.end()) {

                VkDescriptorPoolSize poolSize = {};
                poolSize.descriptorCount = 1;

                switch (bindingInfo.type) {
                case ShaderBindingType::UniformBuffer:
                    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    break;
                case ShaderBindingType::TextureSampler:
                    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                }

                bindingTypeIndex[type] = descriptorPoolSizes.size();
                descriptorPoolSizes.push_back(poolSize);

            } else {

                size_t index = entry->second;
                VkDescriptorPoolSize& poolSize = descriptorPoolSizes[index];
                poolSize.descriptorCount += 1;
            }
        }

        VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        descriptorPoolCreateInfo.poolSizeCount = descriptorPoolSizes.size();
        descriptorPoolCreateInfo.pPoolSizes = descriptorPoolSizes.data();
        descriptorPoolCreateInfo.maxSets = 1; // TODO: Handle multiple descriptor sets!

        if (vkCreateDescriptorPool(m_device, &descriptorPoolCreateInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to create descriptor pool\n");
        }
    }

    //
    // Create descriptor set(s)
    //
    VkDescriptorSet descriptorSet {};
    {
        // TODO: Handle multiple descriptor sets!

        VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        descriptorSetAllocateInfo.descriptorPool = descriptorPool;
        descriptorSetAllocateInfo.descriptorSetCount = 1;
        descriptorSetAllocateInfo.pSetLayouts = &descriptorSetLayout;

        if (vkAllocateDescriptorSets(m_device, &descriptorSetAllocateInfo, &descriptorSet) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to create descriptor set\n");
        }
    }

    //
    // Update descriptor set(s)
    //
    {
        // TODO: Handle multiple descriptor sets!

        std::vector<VkWriteDescriptorSet> descriptorSetWrites {};
        std::vector<VkDescriptorBufferInfo> descBufferInfos {};
        std::vector<VkDescriptorImageInfo> descImageInfos {};

        const ShaderBindingSet& bindingSet = renderState.shaderBindingSet();
        for (auto& bindingInfo : bindingSet.shaderBindings()) {

            VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            write.dstSet = descriptorSet;

            write.descriptorCount = 1;
            write.dstBinding = bindingInfo.bindingIndex;

            write.dstArrayElement = 0;

            switch (bindingInfo.type) {
            case ShaderBindingType::UniformBuffer: {

                ASSERT(bindingInfo.buffer);
                const BufferInfo& bufferInfo = m_bufferInfos[bindingInfo.buffer->id()];

                VkDescriptorBufferInfo descBufferInfo {};
                descBufferInfo.offset = 0;
                descBufferInfo.range = VK_WHOLE_SIZE;
                descBufferInfo.buffer = bufferInfo.buffer;

                descBufferInfos.push_back(descBufferInfo);
                write.pBufferInfo = &descBufferInfos.back();
                write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

                break;
            }

            case ShaderBindingType::TextureSampler: {

                ASSERT(bindingInfo.texture);
                const TextureInfo& textureInfo = m_textureInfos[bindingInfo.texture->id()];

                VkDescriptorImageInfo descImageInfo {};
                descImageInfo.sampler = textureInfo.sampler;
                descImageInfo.imageView = textureInfo.view;

                ASSERT(textureInfo.currentLayout == VK_IMAGE_LAYOUT_GENERAL || textureInfo.currentLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                descImageInfo.imageLayout = textureInfo.currentLayout;

                descImageInfos.push_back(descImageInfo);
                write.pImageInfo = &descImageInfos.back();
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

                break;
            }
            }

            write.pTexelBufferView = nullptr;

            descriptorSetWrites.push_back(write);
        }

        vkUpdateDescriptorSets(m_device, descriptorSetWrites.size(), descriptorSetWrites.data(), 0, nullptr);
    }

    RenderStateInfo renderStateInfo {};
    renderStateInfo.descriptorSetLayout = descriptorSetLayout;
    renderStateInfo.descriptorSet = descriptorSet;
    renderStateInfo.descriptorPool = descriptorPool;
    renderStateInfo.pipelineLayout = pipelineLayout;
    renderStateInfo.pipeline = graphicsPipeline;

    // TODO: Use free lists!
    size_t index = m_renderStateInfos.size();
    m_renderStateInfos.push_back(renderStateInfo);
    renderState.registerBackend(backendBadge(), index);
}

void VulkanBackend::deleteRenderState(const RenderState& renderState)
{
    if (renderState.id() == Resource::NullId) {
        LogErrorAndExit("Trying to delete an already-deleted or not-yet-created render state\n");
    }

    // TODO: When we have a free list, also maybe remove from the m_renderStateInfos vector? But then we should also keep track of generations etc.
    RenderStateInfo& renderStateInfo = m_renderStateInfos[renderState.id()];

    vkDestroyDescriptorPool(m_device, renderStateInfo.descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(m_device, renderStateInfo.descriptorSetLayout, nullptr);
    vkDestroyPipeline(m_device, renderStateInfo.pipeline, nullptr);
    vkDestroyPipelineLayout(m_device, renderStateInfo.pipelineLayout, nullptr);

    renderState.unregisterBackend(backendBadge());
}

VulkanBackend::RenderStateInfo& VulkanBackend::renderStateInfo(const RenderState& renderState)
{
    RenderStateInfo& renderStateInfo = m_renderStateInfos[renderState.id()];
    return renderStateInfo;
}

void VulkanBackend::reconstructRenderGraph(RenderGraph& renderGraph, const ApplicationState& appState)
{
    // TODO: Implement some kind of smart resource diff where we only delete and create resources that actually change.

    m_frameResourceManagers.resize(m_numSwapchainImages);
    for (uint32_t swapchainImageIndex = 0; swapchainImageIndex < m_numSwapchainImages; ++swapchainImageIndex) {

        auto resourceManager = std::make_unique<ResourceManager>();
        renderGraph.constructAll(*resourceManager, appState);

        // Delete old resources
        if (m_frameResourceManagers[swapchainImageIndex]) {
            auto& previousManager = *m_frameResourceManagers[swapchainImageIndex];

            for (auto& buffer : previousManager.buffers()) {
                deleteBuffer(buffer);
            }
            for (auto& texture : previousManager.textures()) {
                deleteTexture(texture);
            }
            for (auto& renderTarget : previousManager.renderTargets()) {
                deleteRenderTarget(renderTarget);
            }
            for (auto& renderState : previousManager.renderStates()) {
                deleteRenderState(renderState);
            }
        }

        // Create new resources
        for (auto& buffer : resourceManager->buffers()) {
            newBuffer(buffer);
        }
        for (auto& bufferUpdate : resourceManager->bufferUpdates()) {
            updateBuffer(bufferUpdate);
        }
        for (auto& texture : resourceManager->textures()) {
            newTexture(texture);
        }
        for (auto& textureUpdate : resourceManager->textureUpdates()) {
            updateTexture(textureUpdate);
        }
        for (auto& renderTarget : resourceManager->renderTargets()) {
            newRenderTarget(renderTarget);
        }
        for (auto& renderState : resourceManager->renderStates()) {
            newRenderState(renderState, swapchainImageIndex);
        }

        // Replace previous resource manager
        m_frameResourceManagers[swapchainImageIndex] = std::move(resourceManager);
    }
}

void VulkanBackend::destroyRenderGraph(RenderGraph&)
{
    for (uint32_t swapchainImageIndex = 0; swapchainImageIndex < m_numSwapchainImages; ++swapchainImageIndex) {

        if (!m_frameResourceManagers[swapchainImageIndex]) {
            continue;
        }

        auto& oldManager = *m_frameResourceManagers[swapchainImageIndex];

        for (auto& buffer : oldManager.buffers()) {
            deleteBuffer(buffer);
        }
        for (auto& texture : oldManager.textures()) {
            deleteTexture(texture);
        }
        for (auto& renderTarget : oldManager.renderTargets()) {
            deleteRenderTarget(renderTarget);
        }
        for (auto& renderState : oldManager.renderStates()) {
            deleteRenderState(renderState);
        }
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

    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_GENERAL) {

        LogWarning("VulkanBackend::transitionImageLayout(): transitioning to new layout VK_IMAGE_LAYOUT_GENERAL, which isn't great.\n");

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

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
        // TODO/NOTE: This assumes that the image we are copying to has the VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL layout!
        vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    });

    if (!success) {
        LogError("VulkanBackend::copyBufferToImage(): error copying buffer to image, refer to issueSingleTimeCommand errors for more information.\n");
        return false;
    }

    return true;
}

void VulkanBackend::submitQueue(uint32_t imageIndex, VkSemaphore* waitFor, VkSemaphore* signal, VkFence* inFlight)
{
    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitFor;
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_frameCommandBuffers[imageIndex];

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
