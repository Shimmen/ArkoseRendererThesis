#include "VulkanBackend.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "utility/fileio.h"

VulkanBackend::VulkanBackend(GLFWwindow* window)
    : m_window(window)
{
    VkDebugUtilsMessengerCreateInfoEXT dbgMessengerCreateInfo = debugMessengerCreateInfo();

    m_instance = createInstance(&dbgMessengerCreateInfo);
    m_messenger = createDebugMessenger(m_instance, &dbgMessengerCreateInfo);

    m_surface = createSurface(m_instance, window);
    m_physicalDevice = pickBestPhysicalDevice(m_instance, m_surface);

    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &surfaceCapabilities) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend::VulkanBackend(): could not get surface capabilities, exiting.\n");
    }

    m_surfaceFormat = pickBestSurfaceFormat(m_physicalDevice, m_surface);
    m_presentMode = pickBestPresentMode(m_physicalDevice, m_surface);

    findQueueFamilyIndices(m_instance, m_physicalDevice, m_surface);
    m_device = createDevice(m_instance, m_physicalDevice, m_surface);

    vkGetDeviceQueue(m_device, m_graphicsQueueFamilyIndex, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_presentQueueFamilyIndex, 0, &m_presentQueue);
    {
        VkCommandPoolCreateInfo poolCreateInfo = {};
        poolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolCreateInfo.queueFamilyIndex = m_graphicsQueueFamilyIndex;
        poolCreateInfo.flags = 0u;
        if (vkCreateCommandPool(m_device, &poolCreateInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
            LogErrorAndExit("VulkanBackend::VulkanBackend(): could not create command pool for the graphics queue, exiting.\n");
        }
    }

    m_swapchainExtent = pickBestSwapchainExtent(surfaceCapabilities, window);
    m_swapchain = createSwapchain(m_physicalDevice, m_device, m_surface, surfaceCapabilities);

    createSemaphoresAndFences(m_device);

    // FIXME!
    createTheRemainingStuff();

    // TODO: This is the command buffer recording for the example triangle drawing stuff
    for (size_t it = 0; it < m_commandBuffers.size(); ++it) {

        VkCommandBufferBeginInfo commandBufferBeginInfo = {};
        commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandBufferBeginInfo.flags = 0u;
        commandBufferBeginInfo.pInheritanceInfo = nullptr;

        ASSERT(vkBeginCommandBuffer(m_commandBuffers[it], &commandBufferBeginInfo) == VK_SUCCESS);
        {

            VkRenderPassBeginInfo renderPassBeginInfo = {};
            renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassBeginInfo.renderPass = m_exRenderPass;
            renderPassBeginInfo.framebuffer = m_swapchainFramebuffers[it];
            renderPassBeginInfo.renderArea.offset = { 0, 0 };
            renderPassBeginInfo.renderArea.extent = m_swapchainExtent;
            VkClearValue clearColor = { 1.0f, 0.0f, 1.0f, 1.0f };
            renderPassBeginInfo.clearValueCount = 1;
            renderPassBeginInfo.pClearValues = &clearColor;

            vkCmdBeginRenderPass(m_commandBuffers[it], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
            {
                vkCmdBindPipeline(m_commandBuffers[it], VK_PIPELINE_BIND_POINT_GRAPHICS, m_exGraphicsPipeline);
                vkCmdDraw(m_commandBuffers[it], 3, 1, 0, 0);
            }
            vkCmdEndRenderPass(m_commandBuffers[it]);
        }
        ASSERT(vkEndCommandBuffer(m_commandBuffers[it]) == VK_SUCCESS);
    }
}

VulkanBackend::~VulkanBackend()
{
    // Before destroying stuff, make sure it's done with all scheduled work
    vkDeviceWaitIdle(m_device);

    vkDestroyPipeline(m_device, m_exGraphicsPipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_exPipelineLayout, nullptr);
    vkDestroyRenderPass(m_device, m_exRenderPass, nullptr);

    /*
    for (const auto& shaderModule : m_shaderModules) {
        vkDestroyShaderModule(m_device, shaderModule, nullptr);
    }
    */

    for (size_t it = 0; it < maxFramesInFlight; ++it) {
        vkDestroySemaphore(m_device, m_imageAvailableSemaphores[it], nullptr);
        vkDestroySemaphore(m_device, m_renderFinishedSemaphores[it], nullptr);
        vkDestroyFence(m_device, m_inFlightFrameFences[it], nullptr);
    }

    for (size_t it = 0; it < m_numSwapchainImages; ++it) {
        vkDestroyFramebuffer(m_device, m_swapchainFramebuffers[it], nullptr);
        vkDestroyImageView(m_device, m_swapchainImageViews[it], nullptr);
    }

    vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
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

void VulkanBackend::findQueueFamilyIndices(VkInstance instance, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
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

VkDevice VulkanBackend::createDevice(VkInstance instance, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) const
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

    VkDeviceCreateInfo deviceCreateInfo = {};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

    deviceCreateInfo.queueCreateInfoCount = queueCreateInfos.size();
    deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();

    VkPhysicalDeviceFeatures requestedDeviceFeatures = {};
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

VkSwapchainKHR VulkanBackend::createSwapchain(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR surfaceCapabilities)
{
    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;

    // Request one more image than required, if possible (see https://github.com/KhronosGroup/Vulkan-Docs/issues/909 for information)
    createInfo.minImageCount = surfaceCapabilities.minImageCount + 1;
    if (surfaceCapabilities.minImageCount != 0) {
        // (max of zero means no upper limit, so don't clamp in that case)
        createInfo.minImageCount = std::min(createInfo.minImageCount, surfaceCapabilities.maxImageCount);
    }

    createInfo.imageFormat = m_surfaceFormat.format;
    createInfo.imageColorSpace = m_surfaceFormat.colorSpace;

    createInfo.presentMode = m_presentMode;

    createInfo.imageExtent = m_swapchainExtent;
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

    VkSwapchainKHR swapchain;
    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend::createSwapchainAndPopulateImageViews(): could not create swapchain, exiting.\n");
    }

    vkGetSwapchainImagesKHR(device, swapchain, &m_numSwapchainImages, nullptr);
    std::vector<VkImage> swapchainImages(m_numSwapchainImages);
    vkGetSwapchainImagesKHR(device, swapchain, &m_numSwapchainImages, swapchainImages.data());

    m_swapchainImageViews.resize(m_numSwapchainImages);
    for (size_t i = 0; i < swapchainImages.size(); ++i) {

        VkImageViewCreateInfo imageViewCreateInfo = {};
        imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;

        imageViewCreateInfo.image = swapchainImages[i];
        imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewCreateInfo.format = m_surfaceFormat.format;

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
            LogErrorAndExit("VulkanBackend::createSwapchainAndPopulateImageViews(): could not create image view %u (out of %u), exiting.\n", i, m_numSwapchainImages);
        }
    }

    return swapchain;
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

void VulkanBackend::createTheRemainingStuff()
{
    {
        VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
        pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutCreateInfo.setLayoutCount = 0;
        pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
        ASSERT(vkCreatePipelineLayout(m_device, &pipelineLayoutCreateInfo, nullptr, &m_exPipelineLayout) == VK_SUCCESS);

        VkShaderModule vertShaderModule;
        VkPipelineShaderStageCreateInfo vertStageCreateInfo = {};
        {
            auto optionalData = fileio::loadEntireFileAsByteBuffer("shaders/example.vert.spv");
            ASSERT(optionalData.has_value());
            const auto& binaryData = optionalData.value();

            VkShaderModuleCreateInfo moduleCreateInfo = {};
            moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            moduleCreateInfo.codeSize = binaryData.size();
            moduleCreateInfo.pCode = reinterpret_cast<const uint32_t*>(binaryData.data());
            ASSERT(vkCreateShaderModule(m_device, &moduleCreateInfo, nullptr, &vertShaderModule) == VK_SUCCESS);

            vertStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            vertStageCreateInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
            vertStageCreateInfo.module = vertShaderModule;
            vertStageCreateInfo.pName = "main";
        }

        VkShaderModule fragShaderModule;
        VkPipelineShaderStageCreateInfo fragStageCreateInfo = {};
        {
            auto optionalData = fileio::loadEntireFileAsByteBuffer("shaders/example.frag.spv");
            ASSERT(optionalData.has_value());
            const auto& binaryData = optionalData.value();

            VkShaderModuleCreateInfo moduleCreateInfo = {};
            moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            moduleCreateInfo.codeSize = binaryData.size();
            moduleCreateInfo.pCode = reinterpret_cast<const uint32_t*>(binaryData.data());
            ASSERT(vkCreateShaderModule(m_device, &moduleCreateInfo, nullptr, &fragShaderModule) == VK_SUCCESS);

            fragStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            fragStageCreateInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            fragStageCreateInfo.module = fragShaderModule;
            fragStageCreateInfo.pName = "main";
        }

        VkPipelineShaderStageCreateInfo shaderStages[] = { vertStageCreateInfo, fragStageCreateInfo };

        // Setup fixed functions

        VkPipelineVertexInputStateCreateInfo vertInputState = {};
        vertInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertInputState.vertexBindingDescriptionCount = 0;
        vertInputState.pVertexBindingDescriptions = nullptr;
        vertInputState.vertexAttributeDescriptionCount = 0;
        vertInputState.pVertexAttributeDescriptions = nullptr;

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
        inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssemblyState.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(m_swapchainExtent.width);
        viewport.height = static_cast<float>(m_swapchainExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor = {};
        scissor.offset = { 0, 0 };
        scissor.extent = m_swapchainExtent;

        VkPipelineViewportStateCreateInfo viewportState = {};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer = {};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE; // TODO: Make sure we have a nice value here :)
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling = {};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending = {};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        // TODO: Consider what we want here!
        VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_LINE_WIDTH
        };
        VkPipelineDynamicStateCreateInfo dynamicState = {};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;

        VkAttachmentDescription colorAttachment = {};
        colorAttachment.format = m_surfaceFormat.format;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttachmentRef = {};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;

        VkRenderPassCreateInfo renderPassCreateInfo = {};
        renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassCreateInfo.attachmentCount = 1;
        renderPassCreateInfo.pAttachments = &colorAttachment;
        renderPassCreateInfo.subpassCount = 1;
        renderPassCreateInfo.pSubpasses = &subpass;

        // setup subpass dependency to make sure we have the right stuff before drawing to a swapchain image
        // see https://vulkan-tutorial.com/en/Drawing_a_triangle/Drawing/Rendering_and_presentation for info...
        VkSubpassDependency subpassDependency = {};
        subpassDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        subpassDependency.dstSubpass = 0; // i.e. the first and only subpass we have here
        subpassDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDependency.srcAccessMask = 0;
        subpassDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        renderPassCreateInfo.dependencyCount = 1;
        renderPassCreateInfo.pDependencies = &subpassDependency;

        ASSERT(vkCreateRenderPass(m_device, &renderPassCreateInfo, nullptr, &m_exRenderPass) == VK_SUCCESS);

        VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        // stages
        pipelineCreateInfo.stageCount = 2;
        pipelineCreateInfo.pStages = shaderStages;
        // fixed function stuff
        pipelineCreateInfo.pVertexInputState = &vertInputState;
        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pRasterizationState = &rasterizer;
        pipelineCreateInfo.pMultisampleState = &multisampling;
        pipelineCreateInfo.pDepthStencilState = nullptr;
        pipelineCreateInfo.pColorBlendState = &colorBlending;
        pipelineCreateInfo.pDynamicState = nullptr; //&dynamicState;
        // pipeline layout
        pipelineCreateInfo.layout = m_exPipelineLayout;
        // render pass stuff
        pipelineCreateInfo.renderPass = m_exRenderPass;
        pipelineCreateInfo.subpass = 0;
        // extra stuff (optional for this)
        pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
        pipelineCreateInfo.basePipelineIndex = -1;

        ASSERT(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_exGraphicsPipeline) == VK_SUCCESS);

        vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
        vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
    }

    m_swapchainFramebuffers.resize(m_numSwapchainImages);
    for (size_t it = 0; it < m_swapchainImageViews.size(); ++it) {
        VkImageView attachments[] = { m_swapchainImageViews[it] };

        VkFramebufferCreateInfo framebufferCreateInfo = {};
        framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferCreateInfo.renderPass = m_exRenderPass;
        framebufferCreateInfo.attachmentCount = 1;
        framebufferCreateInfo.pAttachments = attachments;
        framebufferCreateInfo.width = m_swapchainExtent.width;
        framebufferCreateInfo.height = m_swapchainExtent.height;
        framebufferCreateInfo.layers = 1;

        ASSERT(vkCreateFramebuffer(m_device, &framebufferCreateInfo, nullptr, &m_swapchainFramebuffers[it]) == VK_SUCCESS);
    }

    // Create command buffers (one per swapchain target image)
    m_commandBuffers.resize(m_numSwapchainImages);
    {
        VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
        commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandBufferAllocateInfo.commandPool = m_commandPool;
        commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; // Can be submitted to a queue for execution, but cannot be called from other command buffers.
        commandBufferAllocateInfo.commandBufferCount = m_commandBuffers.size();

        ASSERT(vkAllocateCommandBuffers(m_device, &commandBufferAllocateInfo, m_commandBuffers.data()) == VK_SUCCESS);
    }
}

bool VulkanBackend::compileCommandQueue(const CommandQueue&)
{
    // TODO!
    return false;
}

void VulkanBackend::executeFrame()
{
    uint32_t imageIndex = m_currentFrameIndex % maxFramesInFlight;

    if (vkWaitForFences(m_device, 1, &m_inFlightFrameFences[imageIndex], VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        LogError("VulkanBackend::executeFrame(): error while waiting for in-flight frame fence (frame %u).\n", m_currentFrameIndex);
    }
    if (vkResetFences(m_device, 1, &m_inFlightFrameFences[imageIndex]) != VK_SUCCESS) {
        LogError("VulkanBackend::executeFrame(): error resetting in-flight frame fence (frame %u).\n", m_currentFrameIndex);
    }

    uint32_t swapchainImageIndex;
    vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, m_imageAvailableSemaphores[imageIndex], VK_NULL_HANDLE, &swapchainImageIndex);

    VkSemaphore waitSemaphores[] = { m_imageAvailableSemaphores[imageIndex] };
    VkSemaphore signalSemaphores[] = { m_renderFinishedSemaphores[imageIndex] };

    // Submit command buffer
    // FIXME: This is mostly specific to the example triangle demo thing...
    {
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        submitInfo.pWaitDstStageMask = waitStages;

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_commandBuffers[swapchainImageIndex];

        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFrameFences[imageIndex]) != VK_SUCCESS) {
            LogError("VulkanBackend::executeFrame(): could not submit the graphics queue (frame %u).\n", m_currentFrameIndex);
        }
    }

    // Present results (synced on the semaphores)
    {
        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;

        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &m_swapchain;
        presentInfo.pImageIndices = &swapchainImageIndex;

        if (vkQueuePresentKHR(m_presentQueue, &presentInfo) != VK_SUCCESS) {
            LogError("VulkanBackend::executeFrame(): could not present swapchain (frame %u).\n", m_currentFrameIndex);
        }
    }

    m_currentFrameIndex += 1;
}

//

ShaderID VulkanBackend::loadShader(const std::string& shaderName)
{
    ShaderStageType stageType = stageTypeForShaderName(shaderName);
    if (stageType == ShaderStageType::Unknown) {
        LogError("VulkanBackend::loadShader(): unknown shader stage type for shader name '%s'.\n", shaderName.c_str());
        return NullShaderID;
    }
    /*
    auto mapResult = m_shaderIdForName.find(shaderName);
    if (mapResult != m_shaderIdForName.end()) {
        return mapResult->second;
    }
    */
    auto fileName = fileNameForShaderName(shaderName);
    auto optionalData = fileio::loadEntireFileAsByteBuffer(fileName);
    if (!optionalData.has_value()) {
        LogError("VulkanBackend::loadShader(): could not load '%s'.\n", shaderName.c_str());
        return NullShaderID;
    }

    const auto& binaryData = optionalData.value();

    VkShaderModuleCreateInfo moduleCreateInfo = {};
    moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleCreateInfo.codeSize = binaryData.size();
    moduleCreateInfo.pCode = reinterpret_cast<const uint32_t*>(binaryData.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_device, &moduleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        LogError("VulkanBackend::loadShader(): could not create shader module.\n");
        return NullShaderID;
    }
    /*
    size_t index = m_shaderModules.size();
    auto shaderId = static_cast<ShaderID>(index);
    m_shaderModules.push_back(shaderModule);
*/
    VkPipelineShaderStageCreateInfo stageCreateInfo = {};
    stageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageCreateInfo.stage = vulkanShaderShaderStageFlag(stageType);
    stageCreateInfo.module = shaderModule;
    stageCreateInfo.pName = "main"; // TODO: Allow specifying entry point!
    stageCreateInfo.pSpecializationInfo = nullptr; // TODO: Allow setting constants!

    //m_shaderIdForName.insert({ shaderName, shaderId });

    //return shaderId;
    return NullShaderID; // TODO!
}

std::string VulkanBackend::fileNameForShaderName(const std::string& shaderName) const
{
    return shaderName + ".spv";
}

VkShaderStageFlagBits VulkanBackend::vulkanShaderShaderStageFlag(ShaderStageType type) const
{
    switch (type) {
    case ShaderStageType::VertexShader:
        return VK_SHADER_STAGE_VERTEX_BIT;
    case ShaderStageType::FragmentShader:
        return VK_SHADER_STAGE_FRAGMENT_BIT;
    case ShaderStageType::Unknown:
        ASSERT_NOT_REACHED();
    }
    ASSERT_NOT_REACHED();
}
