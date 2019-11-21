#include "VulkanBackend.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "utility/fileio.h"
#include "utility/logging.h"
#include "utility/util.h"
#include <algorithm>
#include <cstring>
#include <set>

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

    findQueueFamilyIndices(m_physicalDevice, m_surface);
    m_device = createDevice(m_physicalDevice, m_surface);
    createSemaphoresAndFences(m_device);

    m_context = new VulkanContext(m_physicalDevice, m_device);
    createAndSetupSwapchain(m_physicalDevice, m_device, m_surface);
}

VulkanBackend::~VulkanBackend()
{
    // Before destroying stuff, make sure it's done with all scheduled work
    vkDeviceWaitIdle(m_device);

    destroySwapchain();

    delete m_context;

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
    VkDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo = {};

    debugMessengerCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
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

void VulkanBackend::findQueueFamilyIndices(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
{
    uint32_t count;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, queueFamilies.data());

    bool foundGraphicsQueue = false;
    bool foundComputeQueue = false;
    bool foundPresentQueue = false;

    for (uint32_t idx = 0; idx < count; ++idx) {
        const auto& queueFamily = queueFamilies[idx];

        if (!foundGraphicsQueue && queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            m_graphicsQueueFamilyIndex = idx;
            foundGraphicsQueue = true;
        }

        if (!foundComputeQueue && queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) {
            m_computeQueueFamilyIndex = idx;
            foundComputeQueue = true;
        }

        if (!foundPresentQueue) {
            VkBool32 presentSupportForQueue;
            vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, idx, surface, &presentSupportForQueue);
            if (presentSupportForQueue) {
                m_presentQueueFamilyIndex = idx;
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
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "ArkoseRenderer";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "ArkoseRendererEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instanceCreateInfo = {};
    instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
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
    std::set<uint32_t> queueFamilyIndices = { m_graphicsQueueFamilyIndex, m_computeQueueFamilyIndex, m_presentQueueFamilyIndex };
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    const float queuePriority = 1.0f;
    for (uint32_t familyIndex : queueFamilyIndices) {
        VkDeviceQueueCreateInfo queueCreateInfo = {};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;

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

    vkGetDeviceQueue(device, m_presentQueueFamilyIndex, 0, &m_presentQueue);
    return device;
}

void VulkanBackend::createSemaphoresAndFences(VkDevice device)
{
    VkSemaphoreCreateInfo semaphoreCreateInfo = {};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceCreateInfo = {};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
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

    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
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

    uint32_t queueFamilyIndices[] = { m_graphicsQueueFamilyIndex, m_presentQueueFamilyIndex };
    if (m_graphicsQueueFamilyIndex != m_presentQueueFamilyIndex) {
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

        VkImageViewCreateInfo imageViewCreateInfo = {};
        imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;

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

    // FIXME: For now also create a depth image, but later we probably don't want one on the final presentation images
    //  so it doesn't really make sense to have it here anyway. I guess that's an excuse for the code structure.. :)
    // FIXME: Should we add an explicit image transition for the depth image..?
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    m_depthImage = m_context->createImage2D(swapchainExtent.width, swapchainExtent.height, depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_depthImageMemory);
    m_depthImageView = m_context->createImageView2D(m_depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

    m_context->createTheDrawingStuff(surfaceFormat.format, swapchainExtent, m_swapchainImageViews, m_depthImageView, depthFormat);
}

void VulkanBackend::destroySwapchain()
{
    m_context->destroyTheDrawingStuff();

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

bool VulkanBackend::compileCommandSubmitter(const CommandSubmitter&)
{
    // TODO!
    return false;
}

bool VulkanBackend::executeFrame()
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

    m_context->submitQueue(swapchainImageIndex, &m_imageAvailableSemaphores[currentFrameMod], &m_renderFinishedSemaphores[currentFrameMod], &m_inFlightFrameFences[currentFrameMod]);

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
