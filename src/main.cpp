#include "backend/vk/common-vk.h"
#include "common.h"
#include "utility/fileio.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

constexpr const char* applicationName = "VulkanRenderer";

std::vector<const char*> vulkanExtensions()
{
    std::vector<const char*> extensions;

    uint32_t requiredCount;
    const char** requiredExtensions = glfwGetRequiredInstanceExtensions(&requiredCount);
    while (requiredCount--)
        extensions.emplace_back(requiredExtensions[requiredCount]);

    // For debug utils, e.g. error messages
    extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    return extensions;
}

bool validationLayersSupported(const std::vector<const char*>& layers)
{
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    //for (auto availableLayer : availableLayers) {
    //    printf(" %s: %s\n", availableLayer.layerName, availableLayer.description);
    //}

    for (auto requestedLayer : layers) {
        bool found = false;
        for (auto availableLayer : availableLayers) {
            if (std::strcmp(requestedLayer, availableLayer.layerName) == 0) {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}

#pragma clang diagnostic push
#pragma ide diagnostic ignored "hicpp-signed-bitwise"
VkInstance createInstance(VkDebugUtilsMessengerCreateInfoEXT* debugMsgCreateInfo)
{
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pNext = nullptr;
    appInfo.pApplicationName = applicationName;
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = applicationName;
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pNext = debugMsgCreateInfo;
    info.pApplicationInfo = &appInfo;

    auto extensions = vulkanExtensions();
    info.enabledExtensionCount = extensions.size();
    info.ppEnabledExtensionNames = extensions.data();

    std::vector<const char*> validationLayers;
    if (vulkanDebugMode) {
        validationLayers.emplace_back("VK_LAYER_KHRONOS_validation");
        ASSERT(validationLayersSupported(validationLayers));
    }
    info.enabledLayerCount = validationLayers.size();
    info.ppEnabledLayerNames = validationLayers.data();

    VkInstance instance;
    VkResult result = vkCreateInstance(&info, nullptr, &instance);
    ASSERT(result == VK_SUCCESS);

    return instance;
}
#pragma clang diagnostic pop

VkPhysicalDevice getPhysicalDevice(VkInstance instance, VkSurfaceKHR surface)
{
    uint32_t count;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);

    if (count < 1)
        LogErrorAndExit("Could not find any physical devices with Vulkan support!\n");

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());

    if (count > 1)
        LogWarning("More than one physical device available, one will be chosen arbitrarily!\n");

    VkPhysicalDevice physicalDevice = devices[0];

    // Assert that the physical device supports all required queues and assign them
    VkBool32 presentSupport = false;
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());
    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        VkBool32 presentSupportForQueue;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presentSupportForQueue);
        presentSupport = presentSupport || presentSupportForQueue;
    }
    ASSERT(presentSupport);

    // Assert that the physical device has swapchain support
    std::set<std::string> requiredDeviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, availableExtensions.data());
    for (auto& extension : availableExtensions) {
        requiredDeviceExtensions.erase(extension.extensionName);
    }
    ASSERT(requiredDeviceExtensions.empty());

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    LogInfo("Using physical device '%s'\n", props.deviceName);

    return physicalDevice;
}

uint32_t getQueueFamilyIndex(VkInstance instance, VkPhysicalDevice physicalDevice, VkQueueFlagBits typeBits)
{
    uint32_t count;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, queueFamilies.data());

    for (int idx = 0; idx < count; ++idx) {
        const auto& queueFamily = queueFamilies[idx];
        if (queueFamily.queueCount > 0 && queueFamily.queueFlags & typeBits)
            return idx;
    }

    LogErrorAndExit("Could not find requested queue family type!\n");
    ASSERT_NOT_REACHED();
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
    std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}

VkDebugUtilsMessengerEXT setupDebugUtilsMessenger(VkInstance instance, VkDebugUtilsMessengerCreateInfoEXT* createInfo)
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    ASSERT(func);

    VkDebugUtilsMessengerEXT messenger;
    VkResult result = func(instance, createInfo, nullptr, &messenger);
    ASSERT(result == VK_SUCCESS);

    return messenger;
}

void destroyDebugUtilsMessenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger)
{
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    ASSERT(func);
    func(instance, messenger, nullptr);
}

int main()
{
    uint32_t windowWidth = 1200;
    uint32_t windowHeight = 800;

    ASSERT(glfwInit());
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(windowWidth, windowHeight, applicationName, nullptr, nullptr);
    ASSERT(window);

    ASSERT(glfwVulkanSupported());

    VkDebugUtilsMessengerCreateInfoEXT debugMsgCreateInfo = {};
    debugMsgCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugMsgCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugMsgCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugMsgCreateInfo.pfnUserCallback = debugCallback;
    debugMsgCreateInfo.pUserData = nullptr;

    VkInstance instance = createInstance(&debugMsgCreateInfo);
    VkDebugUtilsMessengerEXT messenger = setupDebugUtilsMessenger(instance, &debugMsgCreateInfo);

    VkSurfaceKHR surface;
    ASSERT(glfwCreateWindowSurface(instance, window, nullptr, &surface) == VK_SUCCESS);

    VkPhysicalDevice physicalDevice = getPhysicalDevice(instance, surface);

    // Query for surface capabilities / swapchain stuff
    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    std::vector<VkSurfaceFormatKHR> surfaceFormats;
    std::vector<VkPresentModeKHR> presentModes;
    {
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities);

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
        ASSERT(formatCount > 0);
        surfaceFormats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, surfaceFormats.data());

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
        ASSERT(presentModeCount > 0);
        presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());
    }

    // Make the device
    VkDevice device;
    uint32_t gfxQueueFamilyIdx;
    uint32_t presentQueueFamilyIdx;
    {
        gfxQueueFamilyIdx = getQueueFamilyIndex(instance, physicalDevice, VK_QUEUE_GRAPHICS_BIT);

        VkDeviceQueueCreateInfo gfxQueueCreateInfo = {};
        gfxQueueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        gfxQueueCreateInfo.queueFamilyIndex = gfxQueueFamilyIdx;
        gfxQueueCreateInfo.queueCount = 1;

        // TODO: In the future, be smart and actually allocate all queues that we require instead of doing this..
        VkBool32 presentSupportForQueue;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, gfxQueueFamilyIdx, surface, &presentSupportForQueue);
        ASSERT(presentSupportForQueue);
        presentQueueFamilyIdx = gfxQueueFamilyIdx;

        float gfxQueuePriority = 1.0f;
        gfxQueueCreateInfo.pQueuePriorities = &gfxQueuePriority;

        VkPhysicalDeviceFeatures requestedDeviceFeatures = {};

        VkDeviceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pQueueCreateInfos = &gfxQueueCreateInfo;
        createInfo.queueCreateInfoCount = 1;

        createInfo.pEnabledFeatures = &requestedDeviceFeatures;

        std::vector<const char*> validationLayers;
        if (vulkanDebugMode) {
            validationLayers.emplace_back("VK_LAYER_KHRONOS_validation");
            ASSERT(validationLayersSupported(validationLayers));
        }
        createInfo.enabledLayerCount = validationLayers.size();
        createInfo.ppEnabledLayerNames = validationLayers.data();

        std::vector<const char*> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };
        createInfo.enabledExtensionCount = deviceExtensions.size();
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();

        VkResult result = vkCreateDevice(physicalDevice, &createInfo, nullptr, &device);
        ASSERT(result == VK_SUCCESS);
    }

    // Chose surface format
    VkSurfaceFormatKHR surfaceFormat;
    {
        for (const auto& format : surfaceFormats) {
            if (format.format == VK_FORMAT_B8G8R8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                surfaceFormat = format;
                break;
            }
        }
        // If we didn't find the best one, just chose one..
        surfaceFormat = surfaceFormats[0];
    }

    // Chose presentation mode (FIFO is guaranteed to be available, corresponds to normal VSync stuff)
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    {
        for (const auto& mode : presentModes) {
            // Try to chose the mailbox mode, i.e. use-last-fully-generated-image mode
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                presentMode = mode;
                LogInfo("Using optimal mailbox present mode\n");
                break;
            }
        }

        if (presentMode == VK_PRESENT_MODE_FIFO_KHR) {
            LogInfo("Using standard FIFO present mode\n");
        }
    }

    // Chose swap extent
    VkExtent2D swapchainExtent = {};
    {
        if (surfaceCapabilities.currentExtent.width != UINT32_MAX) {
            // The surface/physical device specified the extent to whatever the window is..
            swapchainExtent = surfaceCapabilities.currentExtent;
            LogInfo("Using optimal window extents for swap chain\n");
        } else {
            // The drivers are flexible, so let's choose something good that is within the legal extents
            swapchainExtent.width = std::clamp(windowWidth, surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width);
            swapchainExtent.height = std::clamp(windowHeight, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height);
            LogInfo("Using specified extents (%u x %u) for swap chain\n", swapchainExtent.width, swapchainExtent.height);
        }
    }

    // Create the swap chain
    VkSwapchainKHR swapchain;
    std::vector<VkImage> swapchainImages;
    {
        // See https://github.com/KhronosGroup/Vulkan-Docs/issues/909 for the +1 trick
        uint32_t imageCount = surfaceCapabilities.minImageCount + 1;
        // Max of zero means no upper limit, so don't clamp in that case
        imageCount = surfaceCapabilities.maxImageCount == 0 ? imageCount : std::min(imageCount, surfaceCapabilities.maxImageCount);

        VkSwapchainCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface;

        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = swapchainExtent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        uint32_t queueFamilyIndices[] = { gfxQueueFamilyIdx, presentQueueFamilyIdx };
        if (gfxQueueFamilyIdx != presentQueueFamilyIdx) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        createInfo.preTransform = surfaceCapabilities.currentTransform;
        createInfo.presentMode = presentMode;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; // opaque swapchain
        createInfo.clipped = VK_TRUE; // clip hidden pixels

        VkResult result = vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain);
        ASSERT(result == VK_SUCCESS);

        uint32_t actualImageCount;
        vkGetSwapchainImagesKHR(device, swapchain, &actualImageCount, nullptr);
        swapchainImages.resize(actualImageCount);
        vkGetSwapchainImagesKHR(device, swapchain, &actualImageCount, swapchainImages.data());
    }

    // Create image views for the swap chain images
    std::vector<VkImageView> swapchainImageViews;
    {
        swapchainImageViews.resize(swapchainImages.size());
        for (size_t i = 0; i < swapchainImages.size(); ++i) {
            VkImageViewCreateInfo createInfo = {};

            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = swapchainImages[i];
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = surfaceFormat.format;

            createInfo.components = { .r = VK_COMPONENT_SWIZZLE_IDENTITY, .g = VK_COMPONENT_SWIZZLE_IDENTITY, .b = VK_COMPONENT_SWIZZLE_IDENTITY, .a = VK_COMPONENT_SWIZZLE_IDENTITY };

            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0; // no mipmapping
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0; // using a single source image, etc.
            createInfo.subresourceRange.layerCount = 1;

            VkResult result = vkCreateImageView(device, &createInfo, nullptr, &swapchainImageViews[i]);
            ASSERT(result == VK_SUCCESS);
        }
    }

    VkQueue graphicsQueue;
    vkGetDeviceQueue(device, gfxQueueFamilyIdx, 0, &graphicsQueue);
    VkQueue presentQueue;
    vkGetDeviceQueue(device, presentQueueFamilyIdx, 0, &presentQueue);

    // Setup shader & pipeline stuff
    std::vector<VkFramebuffer> swapchainFramebuffers;
    VkPipeline graphicsPipeline;
    VkRenderPass renderPass;
    VkPipelineLayout pipelineLayout;
    {
        VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
        pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutCreateInfo.setLayoutCount = 0;
        pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
        ASSERT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout) == VK_SUCCESS);

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
            ASSERT(vkCreateShaderModule(device, &moduleCreateInfo, nullptr, &vertShaderModule) == VK_SUCCESS);

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
            ASSERT(vkCreateShaderModule(device, &moduleCreateInfo, nullptr, &fragShaderModule) == VK_SUCCESS);

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
        viewport.width = static_cast<float>(swapchainExtent.width);
        viewport.height = static_cast<float>(swapchainExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor = {};
        scissor.offset = { 0, 0 };
        scissor.extent = swapchainExtent;

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

        // TODO: Setup render passes, etc.
        VkAttachmentDescription colorAttachment = {};
        colorAttachment.format = surfaceFormat.format;
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

        ASSERT(vkCreateRenderPass(device, &renderPassCreateInfo, nullptr, &renderPass) == VK_SUCCESS);

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
        pipelineCreateInfo.layout = pipelineLayout;
        // render pass stuff
        pipelineCreateInfo.renderPass = renderPass;
        pipelineCreateInfo.subpass = 0;
        // extra stuff (optional for this)
        pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
        pipelineCreateInfo.basePipelineIndex = -1;

        ASSERT(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &graphicsPipeline) == VK_SUCCESS);

        vkDestroyShaderModule(device, vertShaderModule, nullptr);
        vkDestroyShaderModule(device, fragShaderModule, nullptr);
    }

    swapchainFramebuffers.resize(swapchainImageViews.size());
    for (size_t it = 0; it < swapchainImageViews.size(); ++it) {
        VkImageView attachments[] = { swapchainImageViews[it] };

        VkFramebufferCreateInfo framebufferCreateInfo = {};
        framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferCreateInfo.renderPass = renderPass;
        framebufferCreateInfo.attachmentCount = 1;
        framebufferCreateInfo.pAttachments = attachments;
        framebufferCreateInfo.width = swapchainExtent.width;
        framebufferCreateInfo.height = swapchainExtent.height;
        framebufferCreateInfo.layers = 1;

        ASSERT(vkCreateFramebuffer(device, &framebufferCreateInfo, nullptr, &swapchainFramebuffers[it]) == VK_SUCCESS);
    }

    // Create command pool
    VkCommandPool commandPool;
    {
        VkCommandPoolCreateInfo poolCreateInfo = {};
        poolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolCreateInfo.queueFamilyIndex = gfxQueueFamilyIdx;
        poolCreateInfo.flags = 0u;

        ASSERT(vkCreateCommandPool(device, &poolCreateInfo, nullptr, &commandPool) == VK_SUCCESS);
    }

    // Create command buffers (one per swapchain target image)
    std::vector<VkCommandBuffer> commandBuffers(swapchainFramebuffers.size());
    {
        VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
        commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandBufferAllocateInfo.commandPool = commandPool;
        commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; // Can be submitted to a queue for execution, but cannot be called from other command buffers.
        commandBufferAllocateInfo.commandBufferCount = commandBuffers.size();

        ASSERT(vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, commandBuffers.data()) == VK_SUCCESS);
    }

    // Command buffer recording ...
    for (size_t it = 0; it < commandBuffers.size(); ++it) {

        VkCommandBufferBeginInfo commandBufferBeginInfo = {};
        commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandBufferBeginInfo.flags = 0u;
        commandBufferBeginInfo.pInheritanceInfo = nullptr;

        ASSERT(vkBeginCommandBuffer(commandBuffers[it], &commandBufferBeginInfo) == VK_SUCCESS);
        {
            // Make a render pass
            {
                VkRenderPassBeginInfo renderPassBeginInfo = {};
                renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                renderPassBeginInfo.renderPass = renderPass;
                renderPassBeginInfo.framebuffer = swapchainFramebuffers[it];
                renderPassBeginInfo.renderArea.offset = { 0, 0 };
                renderPassBeginInfo.renderArea.extent = swapchainExtent;
                VkClearValue clearColor = { 1.0f, 0.0f, 1.0f, 1.0f };
                renderPassBeginInfo.clearValueCount = 1;
                renderPassBeginInfo.pClearValues = &clearColor;

                vkCmdBeginRenderPass(commandBuffers[it], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
                {
                    vkCmdBindPipeline(commandBuffers[it], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
                    vkCmdDraw(commandBuffers[it], 3, 1, 0, 0);
                }
                vkCmdEndRenderPass(commandBuffers[it]);
            }
        }
        ASSERT(vkEndCommandBuffer(commandBuffers[it]) == VK_SUCCESS);
    }

    constexpr size_t maxFramesInFlight = 2;

    // Create semaphore & fences for synchronizing the swap chain and drawing etc.
    std::vector<VkSemaphore> imageAvailableSemaphores(maxFramesInFlight);
    std::vector<VkSemaphore> renderFinishedSemaphores(maxFramesInFlight);
    std::vector<VkFence> inFlightFences(maxFramesInFlight);
    {
        VkSemaphoreCreateInfo semaphoreCreateInfo = {};
        semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceCreateInfo = {};
        fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (size_t it = 0; it < maxFramesInFlight; ++it) {
            ASSERT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &imageAvailableSemaphores[it]) == VK_SUCCESS);
            ASSERT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &renderFinishedSemaphores[it]) == VK_SUCCESS);
            ASSERT(vkCreateFence(device, &fenceCreateInfo, nullptr, &inFlightFences[it]) == VK_SUCCESS);
        }
    }

    printf("Main loop begin\n");

    size_t currentFrameIndex = 0;
    while (!glfwWindowShouldClose(window)) {

        ASSERT(vkWaitForFences(device, 1, &inFlightFences[currentFrameIndex], VK_TRUE, UINT64_MAX) == VK_SUCCESS);
        ASSERT(vkResetFences(device, 1, &inFlightFences[currentFrameIndex]) == VK_SUCCESS);

        glfwPollEvents();

        uint32_t swapchainImageIndex;
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphores[currentFrameIndex], VK_NULL_HANDLE, &swapchainImageIndex);

        VkSemaphore waitSemaphores[] = { imageAvailableSemaphores[currentFrameIndex] };
        VkSemaphore signalSemaphores[] = { renderFinishedSemaphores[currentFrameIndex] };

        // Submit command buffer
        {
            VkSubmitInfo submitInfo = {};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

            VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = waitSemaphores;
            submitInfo.pWaitDstStageMask = waitStages;

            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffers[swapchainImageIndex];

            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = signalSemaphores;

            ASSERT(vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrameIndex]) == VK_SUCCESS);
        }

        // Present results (synced on the semaphores)
        {
            VkPresentInfoKHR presentInfo = {};
            presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

            presentInfo.waitSemaphoreCount = 1;
            presentInfo.pWaitSemaphores = signalSemaphores;

            VkSwapchainKHR swapchains[] = { swapchain };
            presentInfo.swapchainCount = 1;
            presentInfo.pSwapchains = swapchains;
            presentInfo.pImageIndices = &swapchainImageIndex;

            // (in case you care about the present results for each swapchain)
            presentInfo.pResults = nullptr;

            VkResult presentResult = vkQueuePresentKHR(presentQueue, &presentInfo);
        }

        currentFrameIndex = (currentFrameIndex + 1) & maxFramesInFlight;
    }
    printf("Main loop end\n");

    // Before destroying stuff, make sure it's done with all of the things it has been scheduled
    vkDeviceWaitIdle(device);

    for (size_t it = 0; it < maxFramesInFlight; ++it) {
        vkDestroySemaphore(device, imageAvailableSemaphores[it], nullptr);
        vkDestroySemaphore(device, renderFinishedSemaphores[it], nullptr);
        vkDestroyFence(device, inFlightFences[it], nullptr);
    }
    vkDestroyCommandPool(device, commandPool, nullptr);
    for (auto framebuffer : swapchainFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    for (auto imageView : swapchainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }
    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    destroyDebugUtilsMessenger(instance, messenger);
    vkDestroyInstance(instance, nullptr);

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
