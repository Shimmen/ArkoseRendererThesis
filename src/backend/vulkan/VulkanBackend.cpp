#include "VulkanBackend.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "VulkanQueueInfo.h"
#include "rendering/ShaderManager.h"
#include "utility/GlobalState.h"
#include "utility/fileio.h"
#include "utility/logging.h"
#include "utility/util.h"
#include <algorithm>
#include <cstring>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <stb_image.h>
#include <unordered_map>
#include <unordered_set>

static bool s_unhandledWindowResize = false;

VulkanBackend::VulkanBackend(GLFWwindow* window, App& app)
    : m_window(window)
    , m_app(app)
{
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    GlobalState::getMutable(backendBadge()).updateWindowExtent({ width, height });
    glfwSetFramebufferSizeCallback(m_window, static_cast<GLFWframebuffersizefun>([](GLFWwindow* window, int width, int height) {
        GlobalState::getMutable(backendBadge()).updateWindowExtent({ width, height });
        s_unhandledWindowResize = true;
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

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = m_physicalDevice;
    allocatorInfo.device = m_device;
    allocatorInfo.flags = 0u;
    if (vmaCreateAllocator(&allocatorInfo, &m_memoryAllocator) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend::VulkanBackend(): could not create memory allocator, exiting.\n");
    }

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
    createWindowRenderTargetFrontend();

    setupDearImgui();

    m_staticResourceManager = std::make_unique<StaticResourceManager>();
    m_renderGraph = std::make_unique<RenderGraph>(m_numSwapchainImages);
    m_app.setup(*m_staticResourceManager, *m_renderGraph);
    createStaticResources();
    reconstructRenderGraphResources(*m_renderGraph);

    for (size_t i = 0; i < m_numSwapchainImages; ++i) {
        auto allocator = std::make_unique<FrameAllocator>(frameAllocatorSize);
        m_frameAllocators.push_back(std::move(allocator));
    }
}

VulkanBackend::~VulkanBackend()
{
    // Before destroying stuff, make sure it's done with all scheduled work
    vkDeviceWaitIdle(m_device);

    destroyDearImgui();

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

    vmaDestroyAllocator(m_memoryAllocator);

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
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT; // TODO: What do we want here? Maybe this suffices?
    // TODO: Assure VK_IMAGE_USAGE_STORAGE_BIT is supported using vkGetPhysicalDeviceSurfaceCapabilitiesKHR & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT

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

    if (m_guiIsSetup) {
        ImGui_ImplVulkan_SetMinImageCount(m_numSwapchainImages);
        updateDearImguiFramebuffers();
    }

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
    createWindowRenderTargetFrontend();

    s_unhandledWindowResize = false;

    return m_swapchainExtent;
}

void VulkanBackend::createWindowRenderTargetFrontend()
{
    ASSERT(m_numSwapchainImages > 0);

    // TODO: This is clearly stupid..
    ResourceManager& badgeGiver = m_staticResourceManager->internal(backendBadge());

    TextureInfo depthInfo {};
    depthInfo.format = m_depthImageFormat;
    depthInfo.image = m_depthImage;
    depthInfo.view = m_depthImageView;
    depthInfo.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (m_swapchainDepthTexture.hasBackend()) {
        m_textureInfos.remove(m_swapchainDepthTexture.id());
    }
    m_swapchainDepthTexture = Texture(badgeGiver.exchangeBadges(backendBadge()), m_swapchainExtent, Texture::Format::Depth32F,
        Texture::Usage::Attachment, Texture::MinFilter::Nearest, Texture::MagFilter::Nearest, Texture::Mipmap::None);
    size_t depthIndex = m_textureInfos.add(depthInfo);
    m_swapchainDepthTexture.registerBackend(backendBadge(), depthIndex);

    m_swapchainColorTextures.resize(m_numSwapchainImages);
    m_swapchainRenderTargets.resize(m_numSwapchainImages);

    for (size_t i = 0; i < m_numSwapchainImages; ++i) {

        TextureInfo colorInfo {};
        colorInfo.format = m_swapchainImageFormat;
        colorInfo.image = m_swapchainImages[i];
        colorInfo.view = m_swapchainImageViews[i];
        colorInfo.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (m_swapchainColorTextures[i].hasBackend()) {
            m_textureInfos.remove(m_swapchainColorTextures[i].id());
        }
        m_swapchainColorTextures[i] = Texture(badgeGiver.exchangeBadges(backendBadge()), m_swapchainExtent, Texture::Format::Unknown,
            Texture::Usage::Attachment, Texture::MinFilter::Nearest, Texture::MagFilter::Nearest, Texture::Mipmap::None);
        size_t colorIndex = m_textureInfos.add(colorInfo);
        m_swapchainColorTextures[i].registerBackend(backendBadge(), colorIndex);

        RenderTargetInfo targetInfo {};
        targetInfo.compatibleRenderPass = m_swapchainRenderPass;
        targetInfo.framebuffer = m_swapchainFramebuffers[i];

        if (m_swapchainRenderTargets[i].hasBackend()) {
            m_renderTargetInfos.remove(m_swapchainRenderTargets[i].id());
        }
        m_swapchainRenderTargets[i] = RenderTarget(badgeGiver.exchangeBadges(backendBadge()), { { RenderTarget::AttachmentType::Color0, &m_swapchainColorTextures[i] }, { RenderTarget::AttachmentType::Depth, &m_swapchainDepthTexture } });
        size_t targetIndex = m_renderTargetInfos.add(targetInfo);
        m_swapchainRenderTargets[i].registerBackend(backendBadge(), targetIndex);
    }
}

void VulkanBackend::setupDearImgui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    //

    ImGui_ImplGlfw_InitForVulkan(m_window, true);

    //

    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };
    VkDescriptorPoolCreateInfo descPoolCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    descPoolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    descPoolCreateInfo.maxSets = 1000 * IM_ARRAYSIZE(poolSizes);
    descPoolCreateInfo.poolSizeCount = (uint32_t)IM_ARRAYSIZE(poolSizes);
    descPoolCreateInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(m_device, &descPoolCreateInfo, nullptr, &m_guiDescriptorPool) != VK_SUCCESS) {
        LogErrorAndExit("DearImGui error while setting up descriptor pool\n");
    }

    //

    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = m_swapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // TODO: Should it really be undefined here?
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = VK_NULL_HANDLE;

    // TODO: Is this needed here??
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
    renderPassCreateInfo.attachmentCount = 1;
    renderPassCreateInfo.pAttachments = &colorAttachment;
    renderPassCreateInfo.subpassCount = 1;
    renderPassCreateInfo.pSubpasses = &subpass;
    renderPassCreateInfo.dependencyCount = 1;
    renderPassCreateInfo.pDependencies = &subpassDependency;

    if (vkCreateRenderPass(m_device, &renderPassCreateInfo, nullptr, &m_guiRenderPass) != VK_SUCCESS) {
        LogErrorAndExit("DearImGui error while setting up render pass\n");
    }

    //

    updateDearImguiFramebuffers();

    //

    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.CheckVkResultFn = [](VkResult result) {
        if (result != VK_SUCCESS) {
            LogErrorAndExit("DearImGui vulkan error!\n");
        }
    };

    initInfo.Instance = m_instance;
    initInfo.PhysicalDevice = m_physicalDevice;
    initInfo.Device = m_device;
    initInfo.Allocator = nullptr;

    initInfo.QueueFamily = m_queueInfo.graphicsQueueFamilyIndex;
    initInfo.Queue = m_graphicsQueue;

    initInfo.MinImageCount = m_numSwapchainImages; // (todo: should this be something different than the actual count??)
    initInfo.ImageCount = m_numSwapchainImages;

    initInfo.DescriptorPool = m_guiDescriptorPool;
    initInfo.PipelineCache = VK_NULL_HANDLE;

    ImGui_ImplVulkan_Init(&initInfo, m_guiRenderPass);

    //

    issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
        ImGui_ImplVulkan_CreateFontsTexture(commandBuffer);
    });
    ImGui_ImplVulkan_DestroyFontUploadObjects();

    //

    m_guiIsSetup = true;
}

void VulkanBackend::destroyDearImgui()
{
    vkDestroyDescriptorPool(m_device, m_guiDescriptorPool, nullptr);
    vkDestroyRenderPass(m_device, m_guiRenderPass, nullptr);
    for (VkFramebuffer framebuffer : m_guiFramebuffers) {
        vkDestroyFramebuffer(m_device, framebuffer, nullptr);
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    m_guiIsSetup = false;
}

void VulkanBackend::updateDearImguiFramebuffers()
{
    for (VkFramebuffer& framebuffer : m_guiFramebuffers) {
        vkDestroyFramebuffer(m_device, framebuffer, nullptr);
    }
    m_guiFramebuffers.clear();

    for (uint32_t idx = 0; idx < m_numSwapchainImages; ++idx) {
        VkFramebufferCreateInfo framebufferCreateInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        framebufferCreateInfo.renderPass = m_guiRenderPass;
        framebufferCreateInfo.attachmentCount = 1;
        framebufferCreateInfo.pAttachments = &m_swapchainImageViews[idx];
        framebufferCreateInfo.width = m_swapchainExtent.width();
        framebufferCreateInfo.height = m_swapchainExtent.height();
        framebufferCreateInfo.layers = 1;

        VkFramebuffer framebuffer;
        if (vkCreateFramebuffer(m_device, &framebufferCreateInfo, nullptr, &framebuffer) != VK_SUCCESS) {
            LogErrorAndExit("DearImGui error while setting up framebuffer\n");
        }
        m_guiFramebuffers.push_back(framebuffer);
    }
}

void VulkanBackend::renderDearImguiFrame(VkCommandBuffer commandBuffer, uint32_t swapchainImageIndex)
{
    VkRenderPassBeginInfo passBeginInfo = {};
    passBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    passBeginInfo.renderPass = m_guiRenderPass;
    passBeginInfo.framebuffer = m_guiFramebuffers[swapchainImageIndex];
    passBeginInfo.renderArea.extent.width = m_swapchainExtent.width();
    passBeginInfo.renderArea.extent.height = m_swapchainExtent.height();
    passBeginInfo.clearValueCount = 0;
    passBeginInfo.pClearValues = nullptr;

    vkCmdBeginRenderPass(commandBuffer, &passBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    vkCmdEndRenderPass(commandBuffer);
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

    AppState appState { m_swapchainExtent, deltaTime, elapsedTime, m_currentFrameIndex };

    uint32_t swapchainImageIndex;
    VkResult acquireResult = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, m_imageAvailableSemaphores[currentFrameMod], VK_NULL_HANDLE, &swapchainImageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        // Since we couldn't acquire an image to draw to, recreate the swapchain and report that it didn't work
        Extent2D newWindowExtent = recreateSwapchain();
        appState = appState.updateWindowExtent(newWindowExtent);
        reconstructRenderGraphResources(*m_renderGraph);
        return false;
    }
    if (acquireResult == VK_SUBOPTIMAL_KHR) {
        // Since we did manage to acquire an image, just roll with it for now, but it will probably resolve itself after presenting
        LogWarning("VulkanBackend::executeFrame(): next image was acquired but it's suboptimal, ignoring.\n");
    }

    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        LogError("VulkanBackend::executeFrame(): error acquiring next swapchain image.\n");
    }

    // We shouldn't use the data from the swapchain image, so we set current layout accordingly (not sure about depth, but sure..)
    const Texture& currentColorTexture = m_swapchainColorTextures[swapchainImageIndex];
    textureInfo(currentColorTexture).currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    textureInfo(m_swapchainDepthTexture).currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    drawFrame(appState, elapsedTime, deltaTime, swapchainImageIndex);

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

        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || s_unhandledWindowResize) {
            Extent2D newWindowExtent = recreateSwapchain();
            appState = appState.updateWindowExtent(newWindowExtent);
            reconstructRenderGraphResources(*m_renderGraph);
        } else if (presentResult != VK_SUCCESS) {
            LogError("VulkanBackend::executeFrame(): could not present swapchain (frame %u).\n", m_currentFrameIndex);
        }
    }

    m_currentFrameIndex += 1;
    return true;
}

void VulkanBackend::drawFrame(const AppState& appState, double elapsedTime, double deltaTime, uint32_t swapchainImageIndex)
{
    ASSERT(m_renderGraph);

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    m_app.update(elapsedTime, deltaTime);

    VkCommandBufferBeginInfo commandBufferBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    commandBufferBeginInfo.flags = 0u;
    commandBufferBeginInfo.pInheritanceInfo = nullptr;

    VkCommandBuffer commandBuffer = m_frameCommandBuffers[swapchainImageIndex];
    if (vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo) != VK_SUCCESS) {
        LogError("VulkanBackend::executeRenderGraph(): error beginning command buffer command!\n");
    }

    executeRenderGraph(appState, *m_renderGraph, commandBuffer, swapchainImageIndex);

    ImGui::Render();
    renderDearImguiFrame(commandBuffer, swapchainImageIndex);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        LogError("VulkanBackend::executeRenderGraph(): error ending command buffer command!\n");
    }
}

void VulkanBackend::executeRenderGraph(const AppState& appState, const RenderGraph& renderGraph, VkCommandBuffer commandBuffer, uint32_t swapchainImageIndex)
{
    FrameAllocator& frameAllocator = *m_frameAllocators[swapchainImageIndex];
    frameAllocator.reset();

    const ResourceManager& associatedResourceManager = *m_frameResourceManagers[swapchainImageIndex];
    renderGraph.forEachNodeInResolvedOrder(associatedResourceManager, [&](const RenderGraphNode& node) {
        CommandList cmdList {};
        node.executeForFrame(appState, cmdList, frameAllocator, swapchainImageIndex);

        bool insideRenderPass = false;
        auto endCurrentRenderPassIfAny = [&]() { // TODO: Maybe we want this to be more explicit for the app? Or maybe we just warn that it does happen?
            if (insideRenderPass) {
                vkCmdEndRenderPass(commandBuffer);
                insideRenderPass = false;
            }
        };

        while (cmdList.hasNext()) {

            const auto& command = cmdList.next();

            if (command.is<CmdSetRenderState>()) {
                auto& cmd = command.as<CmdSetRenderState>();
                if (cmdList.hasNext() && cmdList.peekNext().is<CmdClear>()) {
                    executeSetRenderState(commandBuffer, cmd, &cmdList.next().as<CmdClear>());
                } else {
                    executeSetRenderState(commandBuffer, cmd, nullptr);
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

            else if (command.is<CmdDrawArray>()) {
                executeDrawArray(commandBuffer, command.as<CmdDrawArray>());
            }

            else if (command.is<CmdDrawIndexed>()) {
                ASSERT(insideRenderPass);
                executeDrawIndexed(commandBuffer, command.as<CmdDrawIndexed>());
            }

            else if (command.is<CmdCopyTexture>()) {
                endCurrentRenderPassIfAny();
                executeCopyTexture(commandBuffer, command.as<CmdCopyTexture>());
            }

            else {
                LogError("VulkanBackend::executeRenderGraph(): unhandled command!\n");
                ASSERT_NOT_REACHED();
            }
        }

        endCurrentRenderPassIfAny();

        // TODO: Currently this doesn't actually need to be here, but soon enough it might be critical! So I'll leave it here for now.
        executeRenderGraphNodeBarrier(commandBuffer);
    });
}
void VulkanBackend::executeRenderGraphNodeBarrier(VkCommandBuffer commandBuffer)
{
    // TODO: This probably doesn't need to be this harsh!
    VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    // TODO: This probably doesn't need to be this harsh!
    VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0,
        1, &barrier,
        0, nullptr,
        0, nullptr);
}

void VulkanBackend::executeSetRenderState(VkCommandBuffer commandBuffer, const CmdSetRenderState& cmd, const CmdClear* clearCmd)
{
    const RenderTarget& renderTarget = cmd.renderState.renderTarget();

    // TODO: Now in hindsight it looks like maybe we should have separate renderTarget & pipelineState commands maybe?
    std::vector<VkClearValue> clearValues {};

    if (clearCmd) {
        VkClearColorValue clearColorValue = { { clearCmd->clearColor.r, clearCmd->clearColor.g, clearCmd->clearColor.b, clearCmd->clearColor.a } };
        VkClearDepthStencilValue clearDepthStencilValue = { clearCmd->clearDepth, clearCmd->clearStencil };

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

    VkRenderPassBeginInfo renderPassBeginInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };

    const RenderTargetInfo& targetInfo = renderTargetInfo(renderTarget);

    // (there is automatic image layout transitions for attached textures, so when we bind the
    //  render target here, make sure to also swap to the new layout in the cache variable)
    for (const auto& attachedTexture : targetInfo.attachedTextures) {
        textureInfo(*attachedTexture).currentLayout = attachedTexture->hasDepthFormat()
            ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    // Explicitly transition the layouts of the sampled textures to an optimal layout (if it isn't already)
    {
        RenderStateInfo& stateInfo = renderStateInfo(cmd.renderState);
        for (const Texture* texture : stateInfo.sampledTextures) {
            auto& texInfo = textureInfo(*texture);
            if (texInfo.currentLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                transitionImageLayout(texInfo.image, texInfo.format, texInfo.currentLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &commandBuffer);
            }
            texInfo.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
    }

    renderPassBeginInfo.renderPass = targetInfo.compatibleRenderPass;
    renderPassBeginInfo.framebuffer = targetInfo.framebuffer;

    auto& targetExtent = renderTarget.extent();
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

void VulkanBackend::executeCopyTexture(VkCommandBuffer commandBuffer, const CmdCopyTexture& command)
{
    ASSERT_NOT_REACHED();
    /*
    const TextureInfo& srcInfo = textureInfo(command.srcTexture);
    const TextureInfo& dstInfo = textureInfo(command.dstTexture);

    VkImageMemoryBarrier imageBarriers[2];
    {
        VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };

        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        barrier.image = srcInfo.image;
        barrier.oldLayout = srcInfo.currentLayout;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; // TODO: Don't assume color texture!
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        imageBarriers[0] = barrier;
    }
    {
        VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };

        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        barrier.image = dstInfo.image;
        barrier.oldLayout = dstInfo.currentLayout;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; // TODO: Don't assume color texture!
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        imageBarriers[1] = barrier;
    }

    VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT; // TODO: Extreme..? yes
    VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0,
        0, nullptr,
        0, nullptr,
        2, imageBarriers);

    // TODO: We probably want to support more options here or in other versions of this

    VkImageCopy copyRegion = {};

    ASSERT(command.srcTexture.extent() == command.dstTexture.extent());
    copyRegion.extent = { command.srcTexture.extent().width(), command.srcTexture.extent().height() };
    copyRegion.srcOffset = { 0, 0, 0 };
    copyRegion.dstOffset = { 0, 0, 0 };

    ASSERT(!command.srcTexture.hasMipmaps() && !command.dstTexture.hasMipmaps());

    bool compatibleFormats = true; // TODO: Actually check!
    ASSERT(compatibleFormats);

    VkImageSubresourceLayers srcSubresource = {};
    srcSubresource.baseArrayLayer = 0;
    srcSubresource.layerCount = 1;
    srcSubresource.mipLevel = 0;
    srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    VkImageSubresourceLayers dstSubresource = {};
    dstSubresource.baseArrayLayer = 0;
    dstSubresource.layerCount = 1;
    dstSubresource.mipLevel = 0;
    dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    copyRegion.srcSubresource = srcSubresource;
    copyRegion.dstSubresource = dstSubresource;

    vkCmdCopyImage(commandBuffer, srcInfo.image, srcInfo.currentLayout, dstInfo.image, dstInfo.currentLayout, 1, &copyRegion);
    */
}

void VulkanBackend::executeDrawArray(VkCommandBuffer commandBuffer, const CmdDrawArray& command)
{
    VkBuffer vertexBuffer = bufferInfo(command.vertexBuffer).buffer;

    VkBuffer vertexBuffers[] = { vertexBuffer };
    VkDeviceSize offsets[] = { 0 };

    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdDraw(commandBuffer, command.vertexCount, 1, 0, 0);
}

void VulkanBackend::executeDrawIndexed(VkCommandBuffer commandBuffer, const CmdDrawIndexed& command)
{
    VkBuffer vertexBuffer = bufferInfo(command.vertexBuffer).buffer;
    VkBuffer indexBuffer = bufferInfo(command.indexBuffer).buffer;

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

    VmaAllocationCreateInfo allocCreateInfo = {};
    switch (buffer.memoryHint()) {
    case Buffer::MemoryHint::GpuOnly:
        allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        break;
    case Buffer::MemoryHint::GpuOptimal:
        allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        usageFlags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        break;
    case Buffer::MemoryHint::TransferOptimal:
        allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU; // (ensures host visible!)
        allocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        break;
    }

    VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferCreateInfo.size = buffer.size();
    bufferCreateInfo.usage = usageFlags;

    VkBuffer vkBuffer;
    VmaAllocation allocation;
    VmaAllocationInfo allocationInfo;
    if (vmaCreateBuffer(m_memoryAllocator, &bufferCreateInfo, &allocCreateInfo, &vkBuffer, &allocation, &allocationInfo) != VK_SUCCESS) {
        LogError("VulkanBackend::newBuffer(): could not create buffer of size %u.\n", buffer.size());
    }

    BufferInfo bufferInfo {};
    bufferInfo.buffer = vkBuffer;
    bufferInfo.allocation = allocation;

    size_t index = m_bufferInfos.add(bufferInfo);
    buffer.registerBackend(backendBadge(), index);
}

void VulkanBackend::deleteBuffer(const Buffer& buffer)
{
    if (buffer.id() == Resource::NullId) {
        LogErrorAndExit("Trying to delete an already-deleted or not-yet-created buffer\n");
    }

    const BufferInfo& bufInfo = bufferInfo(buffer);
    vmaDestroyBuffer(m_memoryAllocator, bufInfo.buffer, bufInfo.allocation);

    m_bufferInfos.remove(buffer.id());
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

    BufferInfo& bufInfo = bufferInfo(buffer);

    switch (buffer.memoryHint()) {
    case Buffer::MemoryHint::GpuOptimal:
        if (!setBufferDataUsingStagingBuffer(bufInfo.buffer, data, size)) {
            LogError("VulkanBackend::updateBuffer(): could not update the buffer memory through staging buffer.\n");
        }
        break;
    case Buffer::MemoryHint::TransferOptimal:
        if (!setBufferMemoryUsingMapping(bufInfo.allocation, data, size)) {
            LogError("VulkanBackend::updateBuffer(): could not update the buffer memory through mapping.\n");
        }
        break;
    case Buffer::MemoryHint::GpuOnly:
        LogError("VulkanBackend::updateBuffer(): can't update buffer with GpuOnly memory hint, ignoring\n");
        break;
    }
}

VulkanBackend::BufferInfo& VulkanBackend::bufferInfo(const Buffer& buffer)
{
    BufferInfo& bufferInfo = m_bufferInfos[buffer.id()];
    return bufferInfo;
}

void VulkanBackend::newTexture(const Texture& texture)
{
    VkFormat format;
    switch (texture.format()) {
    case Texture::Format::RGB8:
        format = VK_FORMAT_R8G8B8_UNORM;
        break;
    case Texture::Format::RGBA8:
        format = VK_FORMAT_R8G8B8A8_UNORM;
        break;
    case Texture::Format::sRGBA8:
        format = VK_FORMAT_R8G8B8A8_SRGB;
        break;
    case Texture::Format::Depth32F:
        format = VK_FORMAT_D32_SFLOAT;
        break;
    case Texture::Format::Unknown:
        LogErrorAndExit("Trying to create new texture with format Unknown, which is not allowed!\n");
    }

    const VkImageUsageFlags attachmentFlags = texture.hasDepthFormat() ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    const VkImageUsageFlags sampledFlags = VK_IMAGE_USAGE_SAMPLED_BIT;

    VkImageUsageFlags usageFlags = 0u;
    switch (texture.usage()) {
    case Texture::Usage::Attachment:
        usageFlags = attachmentFlags;
        break;
    case Texture::Usage::Sampled:
        usageFlags = sampledFlags;
        break;
    case Texture::Usage::All:
        // TODO: Something more that needs to be here?
        usageFlags = attachmentFlags | sampledFlags;
        break;
    }

    // (if we later want to generate mipmaps we need the ability to use each mip as a src & dst in blitting)
    if (texture.hasMipmaps()) {
        usageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        usageFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    // TODO: For now always keep images in device local memory.
    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    usageFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    VkImageCreateInfo imageCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.extent = { .width = texture.extent().width(), .height = texture.extent().height(), .depth = 1 };
    imageCreateInfo.mipLevels = texture.mipLevels();
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.usage = usageFlags;
    imageCreateInfo.format = format;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkImage image;
    VmaAllocation allocation;
    if (vmaCreateImage(m_memoryAllocator, &imageCreateInfo, &allocCreateInfo, &image, &allocation, nullptr) != VK_SUCCESS) {
        LogError("VulkanBackend::newTexture(): could not create image.\n");
    }

    // TODO: Handle things like mipmaps here!
    VkImageAspectFlags aspectFlags = 0u;
    if (texture.hasDepthFormat()) {
        aspectFlags |= VK_IMAGE_ASPECT_DEPTH_BIT;
    } else {
        aspectFlags |= VK_IMAGE_ASPECT_COLOR_BIT;
    }

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
    viewCreateInfo.subresourceRange.levelCount = texture.mipLevels();
    viewCreateInfo.subresourceRange.baseArrayLayer = 0;
    viewCreateInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(m_device, &viewCreateInfo, nullptr, &imageView) != VK_SUCCESS) {
        LogError("VulkanBackend::newTexture(): could not create image view.\n");
    }

    VkFilter minFilter;
    switch (texture.minFilter()) {
    case Texture::MinFilter::Linear:
        minFilter = VK_FILTER_LINEAR;
        break;
    case Texture::MinFilter::Nearest:
        minFilter = VK_FILTER_NEAREST;
        break;
    }

    VkFilter magFilter;
    switch (texture.magFilter()) {
    case Texture::MagFilter::Linear:
        magFilter = VK_FILTER_LINEAR;
        break;
    case Texture::MagFilter::Nearest:
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

    samplerCreateInfo.mipLodBias = 0.0f;
    samplerCreateInfo.minLod = 0.0f;
    switch (texture.mipmap()) {
    case Texture::Mipmap::None:
        samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerCreateInfo.maxLod = 0.0f;
        break;
    case Texture::Mipmap::Nearest:
        samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerCreateInfo.maxLod = texture.mipLevels();
        break;
    case Texture::Mipmap::Linear:
        samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerCreateInfo.maxLod = texture.mipLevels();
        break;
    }

    VkSampler sampler;
    if (vkCreateSampler(m_device, &samplerCreateInfo, nullptr, &sampler) != VK_SUCCESS) {
        LogError("VulkanBackend::newTexture(): could not create sampler for the image.\n");
    }

    VkImageLayout layout;
    switch (texture.usage()) {
    case Texture::Usage::Attachment:
        layout = texture.hasDepthFormat() ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        break;
    case Texture::Usage::Sampled:
        layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        break;
    case Texture::Usage::All:
        layout = VK_IMAGE_LAYOUT_GENERAL;
        break;
    }
    {
        VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageBarrier.newLayout = layout;
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        imageBarrier.image = image;
        imageBarrier.subresourceRange.aspectMask = texture.hasDepthFormat() ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        imageBarrier.subresourceRange.baseMipLevel = 0;
        imageBarrier.subresourceRange.levelCount = texture.mipLevels();
        imageBarrier.subresourceRange.baseArrayLayer = 0;
        imageBarrier.subresourceRange.layerCount = 1;

        // NOTE: This is very slow, since it blocks everything, but it should be fine for this purpose
        VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

        bool success = issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
            vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0,
                0, nullptr,
                0, nullptr,
                1, &imageBarrier);
        });
        if (!success) {
            LogErrorAndExit("VulkanBackend::newTexture():could not transition image to the preferred layout.\n");
        }
    }

    TextureInfo textureInfo {};
    textureInfo.image = image;
    textureInfo.allocation = allocation;
    textureInfo.format = format;
    textureInfo.view = imageView;
    textureInfo.sampler = sampler;
    textureInfo.currentLayout = layout;

    size_t index = m_textureInfos.add(textureInfo);
    texture.registerBackend(backendBadge(), index);
}

void VulkanBackend::deleteTexture(const Texture& texture)
{
    if (texture.id() == Resource::NullId) {
        LogErrorAndExit("Trying to delete an already-deleted or not-yet-created texture\n");
    }

    TextureInfo& texInfo = textureInfo(texture);
    vkDestroySampler(m_device, texInfo.sampler, nullptr);
    vkDestroyImageView(m_device, texInfo.view, nullptr);
    vmaDestroyImage(m_memoryAllocator, texInfo.image, texInfo.allocation);

    m_textureInfos.remove(texture.id());
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

    int numChannels;
    switch (update.texture().format()) {
    case Texture::Format::RGB8:
        numChannels = 3;
        break;
    case Texture::Format::RGBA8:
        numChannels = 4;
        break;
    case Texture::Format::sRGBA8:
        numChannels = 4;
        break;
    case Texture::Format::Depth32F:
        numChannels = 1;
        break;
    case Texture::Format::Unknown:
        ASSERT_NOT_REACHED();
        break;
    }

    int width, height;
    stbi_uc* pixels = stbi_load(update.path().c_str(), &width, &height, nullptr, numChannels);
    if (!pixels) {
        LogError("VulkanBackend::updateTexture(): stb_image could not read the contents of '%s'.\n", update.path().c_str());
        return;
    }

    if (Extent2D(width, height) != update.texture().extent()) {
        LogErrorAndExit("VulkanBackend::updateTexture(): loaded texture does not match specified extent.\n");
    }

    VkDeviceSize pixelsSize = width * height * numChannels * sizeof(stbi_uc);

    VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferCreateInfo.size = pixelsSize;

    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    if (vmaCreateBuffer(m_memoryAllocator, &bufferCreateInfo, &allocCreateInfo, &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        LogError("VulkanBackend::updateTexture(): could not create staging buffer.\n");
    }

    if (!setBufferMemoryUsingMapping(stagingAllocation, pixels, pixelsSize)) {
        LogError("VulkanBackend::updateTexture(): could set the buffer memory for the staging buffer.\n");
        return;
    }

    AT_SCOPE_EXIT([&]() {
        vmaDestroyBuffer(m_memoryAllocator, stagingBuffer, stagingAllocation);
        stbi_image_free(pixels);
    });

    TextureInfo& texInfo = textureInfo(update.texture());

    // NOTE: Since we are updating the texture we don't care what was in the image before. For these cases undefined
    //  works fine, since it will simply discard/ignore whatever data is in it before.
    VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (!transitionImageLayout(texInfo.image, texInfo.format, oldLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)) {
        LogError("VulkanBackend::updateTexture(): could not transition the image to transfer layout.\n");
    }
    if (!copyBufferToImage(stagingBuffer, texInfo.image, width, height)) {
        LogError("VulkanBackend::updateTexture(): could not copy the staging buffer to the image.\n");
    }

    VkImageLayout finalLayout;
    switch (update.texture().usage()) {
    case Texture::Usage::Attachment:
        finalLayout = update.texture().hasDepthFormat() ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        break;
    case Texture::Usage::Sampled:
        finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        break;
    case Texture::Usage::All:
        finalLayout = VK_IMAGE_LAYOUT_GENERAL;
        break;
    }
    texInfo.currentLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    if (update.generateMipmaps()) {
        generateMipmaps(update.texture(), finalLayout);
    } else {
        if (!transitionImageLayout(texInfo.image, texInfo.format, texInfo.currentLayout, finalLayout)) {
            LogError("VulkanBackend::updateTexture(): could not transition the image to the final image layout.\n");
        }
    }
    texInfo.currentLayout = finalLayout;
}

void VulkanBackend::generateMipmaps(const Texture& texture, VkImageLayout finalLayout)
{
    ASSERT(texture.hasMipmaps());
    TextureInfo& texInfo = textureInfo(texture);
    VkImage image = texInfo.image;

    VkImageAspectFlagBits aspectMask = texture.hasDepthFormat() ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.subresourceRange.aspectMask = aspectMask;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    uint32_t mipLevels = texture.mipLevels();
    int32_t mipWidth = texture.extent().width();
    int32_t mipHeight = texture.extent().height();

    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    ASSERT(texInfo.currentLayout != VK_IMAGE_LAYOUT_UNDEFINED);

    bool success = issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
        // Transition mips 1-n to transfer dst optimal
        {
            VkImageMemoryBarrier initialBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            initialBarrier.image = image;
            initialBarrier.subresourceRange.aspectMask = aspectMask;
            initialBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            initialBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            initialBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            initialBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            initialBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initialBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initialBarrier.subresourceRange.baseArrayLayer = 0;
            initialBarrier.subresourceRange.layerCount = 1;
            initialBarrier.subresourceRange.baseMipLevel = 1;
            initialBarrier.subresourceRange.levelCount = texture.mipLevels() - 1;

            vkCmdPipelineBarrier(commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                0, nullptr,
                0, nullptr,
                1, &initialBarrier);
        }

        for (uint32_t i = 1; i < mipLevels; ++i) {

            int32_t nextWidth = mipWidth > 1 ? mipWidth / 2 : 1;
            int32_t nextHeight = mipHeight > 1 ? mipHeight / 2 : 1;

            // TextureInfo's currentLayout keeps track of the whole image (or kind of mip0) but when we are messing
            // with it here, it will have to be different for the different mip levels.
            VkImageLayout currentLayout = (i == 1) ? texInfo.currentLayout : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = currentLayout;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            vkCmdPipelineBarrier(commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                0, nullptr,
                0, nullptr,
                1, &barrier);

            VkImageBlit blit = {};
            blit.srcOffsets[0] = { 0, 0, 0 };
            blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
            blit.srcSubresource.aspectMask = aspectMask;
            blit.srcSubresource.mipLevel = i - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = 1;
            blit.dstOffsets[0] = { 0, 0, 0 };
            blit.dstOffsets[1] = { nextWidth, nextHeight, 1 };
            blit.dstSubresource.aspectMask = aspectMask;
            blit.dstSubresource.mipLevel = i;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = 1;

            vkCmdBlitImage(commandBuffer,
                image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &blit,
                VK_FILTER_LINEAR);

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = finalLayout;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT, dstStage, 0,
                0, nullptr,
                0, nullptr,
                1, &barrier);

            mipWidth = nextWidth;
            mipHeight = nextHeight;
        }

        barrier.subresourceRange.baseMipLevel = mipLevels - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = finalLayout;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, dstStage, 0,
            0, nullptr,
            0, nullptr,
            1, &barrier);
    });

    if (!success) {
        LogError("VulkanBackend::generateMipmaps(): error while generating mipmaps\n");
    }
}

VulkanBackend::TextureInfo& VulkanBackend::textureInfo(const Texture& texture)
{
    TextureInfo& textureInfo = m_textureInfos[texture.id()];
    return textureInfo;
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

        const TextureInfo& texInfo = textureInfo(*texture);

        // TODO: Handle multisampling, clearing, storing, and stencil stuff!
        VkAttachmentDescription attachment = {};
        attachment.format = texInfo.format;
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

        // TODO/FIXME: This works for now since we have VK_ATTACHMENT_LOAD_OP_CLEAR above, but in the future,
        //  when we don't clear every time this will need to be changed. Using texInfo.currentLayout also won't
        //  work since we only use the layout at the time of creating this render pass, and not what it is in
        //  runtime. Not sure what the best way of doing this is. Maybe always using explicit transitions, and
        //  here just specifying the same initialLayout and finalLayout so nothing(?) happens. Maybe..
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; //texInfo.currentLayout;
        attachment.finalLayout = finalLayout;

        uint32_t attachmentIndex = allAttachments.size();
        allAttachments.push_back(attachment);
        allAttachmentImageViews.push_back(texInfo.view);

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
    for (auto& [_, texture] : renderTarget.sortedAttachments()) {
        renderTargetInfo.attachedTextures.push_back(texture);
    }

    size_t index = m_renderTargetInfos.add(renderTargetInfo);
    renderTarget.registerBackend(backendBadge(), index);
}

void VulkanBackend::deleteRenderTarget(const RenderTarget& renderTarget)
{
    if (renderTarget.id() == Resource::NullId) {
        LogErrorAndExit("Trying to delete an already-deleted or not-yet-created render target\n");
    }

    RenderTargetInfo& targetInfo = renderTargetInfo(renderTarget);
    vkDestroyFramebuffer(m_device, targetInfo.framebuffer, nullptr);
    vkDestroyRenderPass(m_device, targetInfo.compatibleRenderPass, nullptr);

    m_renderTargetInfos.remove(renderTarget.id());
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
    m_swapchainRenderPass = renderPass;

    m_swapchainFramebuffers.resize(m_numSwapchainImages);
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

        m_swapchainFramebuffers[it] = framebuffer;
    }
}

void VulkanBackend::destroyWindowRenderTargets()
{
    for (RenderTarget& renderTarget : m_swapchainRenderTargets) {
        RenderTargetInfo& info = renderTargetInfo(renderTarget);
        vkDestroyFramebuffer(m_device, info.framebuffer, nullptr);
    }

    RenderTargetInfo& info = renderTargetInfo(m_swapchainRenderTargets[0]);
    vkDestroyRenderPass(m_device, info.compatibleRenderPass, nullptr);
}

VulkanBackend::RenderTargetInfo& VulkanBackend::renderTargetInfo(const RenderTarget& renderTarget)
{
    RenderTargetInfo& renderTargetInfo = m_renderTargetInfos[renderTarget.id()];
    return renderTargetInfo;
}

void VulkanBackend::newRenderState(const RenderState& renderState)
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
            switch (file.stage()) {
            case ShaderStage::Vertex:
                stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
                break;
            case ShaderStage::Fragment:
                stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
                break;
            case ShaderStage::Compute:
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

            switch (bindingInfo.shaderStage) {
            case ShaderStage::Vertex:
                binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
                break;
            case ShaderStage::Fragment:
                binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
                break;
            case ShaderStage::Compute:
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
    viewport.width = viewportInfo.extent.width();
    viewport.height = viewportInfo.extent.height();
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    // TODO: Should we always use the viewport settings if no scissor is specified?
    VkRect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent.width = uint32_t(viewportInfo.extent.width());
    scissor.extent.height = uint32_t(viewportInfo.extent.height());

    VkPipelineViewportStateCreateInfo viewportState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.lineWidth = 1.0f;

    switch (renderState.rasterState().polygonMode) {
    case PolygonMode::Filled:
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        break;
    case PolygonMode::Lines:
        rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
        break;
    case PolygonMode::Points:
        rasterizer.polygonMode = VK_POLYGON_MODE_POINT;
        break;
    }

    if (renderState.rasterState().backfaceCullingEnabled) {
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    } else {
        rasterizer.cullMode = VK_CULL_MODE_NONE;
    }

    switch (renderState.rasterState().frontFace) {
    case TriangleWindingOrder::Clockwise:
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        break;
    case TriangleWindingOrder::CounterClockwise:
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        break;
    }

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
    const RenderTargetInfo& targetInfo = renderTargetInfo(renderTarget);

    pipelineCreateInfo.renderPass = targetInfo.compatibleRenderPass;
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
                const BufferInfo& bufInfo = bufferInfo(*bindingInfo.buffer);

                VkDescriptorBufferInfo descBufferInfo {};
                descBufferInfo.offset = 0;
                descBufferInfo.range = VK_WHOLE_SIZE;
                descBufferInfo.buffer = bufInfo.buffer;

                descBufferInfos.push_back(descBufferInfo);
                write.pBufferInfo = &descBufferInfos.back();
                write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

                break;
            }

            case ShaderBindingType::TextureSampler: {

                ASSERT(bindingInfo.texture);
                const TextureInfo& texInfo = textureInfo(*bindingInfo.texture);

                VkDescriptorImageInfo descImageInfo {};
                descImageInfo.sampler = texInfo.sampler;
                descImageInfo.imageView = texInfo.view;

                //ASSERT(texInfo.currentLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                ASSERT(bindingInfo.texture->usage() == Texture::Usage::Sampled || bindingInfo.texture->usage() == Texture::Usage::All);
                descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;//texInfo.currentLayout;

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

    for (auto& bindingInfo : renderState.shaderBindingSet().shaderBindings()) {
        if (bindingInfo.type == ShaderBindingType::TextureSampler) {
            renderStateInfo.sampledTextures.push_back(bindingInfo.texture);
        }
    }

    size_t index = m_renderStateInfos.add(renderStateInfo);
    renderState.registerBackend(backendBadge(), index);
}

void VulkanBackend::deleteRenderState(const RenderState& renderState)
{
    if (renderState.id() == Resource::NullId) {
        LogErrorAndExit("Trying to delete an already-deleted or not-yet-created render state\n");
    }

    RenderStateInfo& stateInfo = renderStateInfo(renderState);
    vkDestroyDescriptorPool(m_device, stateInfo.descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(m_device, stateInfo.descriptorSetLayout, nullptr);
    vkDestroyPipeline(m_device, stateInfo.pipeline, nullptr);
    vkDestroyPipelineLayout(m_device, stateInfo.pipelineLayout, nullptr);

    m_renderStateInfos.remove(renderState.id());
    renderState.unregisterBackend(backendBadge());
}

VulkanBackend::RenderStateInfo& VulkanBackend::renderStateInfo(const RenderState& renderState)
{
    RenderStateInfo& renderStateInfo = m_renderStateInfos[renderState.id()];
    return renderStateInfo;
}

void VulkanBackend::reconstructRenderGraphResources(RenderGraph& renderGraph)
{
    // TODO: Implement some kind of smart resource diff where we only delete and create resources that actually change.

    m_frameResourceManagers.resize(m_numSwapchainImages);
    for (uint32_t swapchainImageIndex = 0; swapchainImageIndex < m_numSwapchainImages; ++swapchainImageIndex) {

        const RenderTarget& windowRenderTargetForFrame = m_swapchainRenderTargets[swapchainImageIndex];
        auto resourceManager = std::make_unique<ResourceManager>(&windowRenderTargetForFrame);
        renderGraph.constructAllForFrame(*resourceManager, swapchainImageIndex);

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
            newRenderState(renderState);
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

bool VulkanBackend::setBufferMemoryUsingMapping(VmaAllocation allocation, const void* data, VkDeviceSize size)
{
    void* mappedMemory;
    if (vmaMapMemory(m_memoryAllocator, allocation, &mappedMemory) != VK_SUCCESS) {
        LogError("VulkanBackend::setBufferMemoryUsingMapping(): could not map staging buffer.\n");
        return false;
    }
    std::memcpy(mappedMemory, data, size);
    vmaUnmapMemory(m_memoryAllocator, allocation);
    return true;
}

bool VulkanBackend::setBufferDataUsingStagingBuffer(VkBuffer buffer, const void* data, VkDeviceSize size)
{
    VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferCreateInfo.size = size;

    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    if (vmaCreateBuffer(m_memoryAllocator, &bufferCreateInfo, &allocCreateInfo, &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        LogError("VulkanBackend::setBufferDataUsingStagingBuffer(): could not create staging buffer.\n");
    }

    AT_SCOPE_EXIT([&] {
        vmaDestroyBuffer(m_memoryAllocator, stagingBuffer, stagingAllocation);
    });

    if (!setBufferMemoryUsingMapping(stagingAllocation, data, size)) {
        LogError("VulkanBackend::setBufferDataUsingStagingBuffer(): could set staging buffer memory.\n");
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

bool VulkanBackend::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, VkCommandBuffer* currentCommandBuffer) const
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

    } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {

        // Wait for all color attachment writes ...
        sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        // ... before allowing any shaders to read the memory
        destinationStage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {

        // Wait for all color attachment writes ...
        sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        // ... before allowing any reading or writing
        destinationStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

    } else {
        LogErrorAndExit("VulkanBackend::transitionImageLayout(): old & new layout combination unsupported by application, exiting.\n");
    }

    if (currentCommandBuffer) {
        vkCmdPipelineBarrier(*currentCommandBuffer, sourceStage, destinationStage, 0,
            0, nullptr,
            0, nullptr,
            1, &imageBarrier);
    } else {
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
