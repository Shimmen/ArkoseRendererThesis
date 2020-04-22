#include "VulkanBackend.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "VulkanCommandList.h"
#include "rendering/Registry.h"
#include "rendering/ShaderManager.h"
#include "utility/FileIO.h"
#include "utility/GlobalState.h"
#include "utility/Logging.h"
#include "utility/util.h"
#include <algorithm>
#include <cstring>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <spirv_cross.hpp>
#include <stb_image.h>
#include <unordered_map>
#include <unordered_set>

#ifdef NDEBUG
static constexpr bool debugMode = false;
#else
static constexpr bool debugMode = true;
#endif

static bool s_unhandledWindowResize = false;

VulkanBackend::VulkanBackend(GLFWwindow* window, App& app)
    : m_window(window)
    , m_app(app)
{
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    GlobalState::getMutable(backendBadge()).updateWindowExtent({ width, height });
    glfwSetFramebufferSizeCallback(window, static_cast<GLFWframebuffersizefun>([](GLFWwindow* window, int width, int height) {
                                       GlobalState::getMutable(backendBadge()).updateWindowExtent({ width, height });
                                       s_unhandledWindowResize = true;
                                   }));

    m_core = std::make_unique<VulkanCore>(window, debugMode);

    createSemaphoresAndFences(device());

    if (VulkanRTX::isSupportedOnPhysicalDevice(physicalDevice())) {
        m_rtx = VulkanRTX(physicalDevice(), device());
    }

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = physicalDevice();
    allocatorInfo.device = device();
    allocatorInfo.flags = 0u;
    if (vmaCreateAllocator(&allocatorInfo, &m_memoryAllocator) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend::VulkanBackend(): could not create memory allocator, exiting.\n");
    }

    m_presentQueue = m_core->presentQueue();
    m_graphicsQueue = m_core->graphicsQueue();

    VkCommandPoolCreateInfo poolCreateInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolCreateInfo.queueFamilyIndex = m_graphicsQueue.familyIndex;
    poolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // (so we can easily reuse them each frame)
    if (vkCreateCommandPool(m_core->device(), &poolCreateInfo, nullptr, &m_renderGraphFrameCommandPool) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend::VulkanBackend(): could not create command pool for the graphics queue, exiting.\n");
    }

    VkCommandPoolCreateInfo transientPoolCreateInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    transientPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    transientPoolCreateInfo.queueFamilyIndex = m_graphicsQueue.familyIndex;
    if (vkCreateCommandPool(m_core->device(), &transientPoolCreateInfo, nullptr, &m_transientCommandPool) != VK_SUCCESS) {
        LogErrorAndExit("VulkanBackend::VulkanBackend(): could not create transient command pool, exiting.\n");
    }

    size_t numEvents = 4;
    m_events.resize(numEvents);
    VkEventCreateInfo eventCreateInfo = { VK_STRUCTURE_TYPE_EVENT_CREATE_INFO };
    for (size_t i = 0; i < numEvents; ++i) {
        if (vkCreateEvent(device(), &eventCreateInfo, nullptr, &m_events[i]) != VK_SUCCESS) {
            LogErrorAndExit("VulkanBackend::VulkanBackend(): could not create event, exiting.\n");
        }
        if (vkSetEvent(device(), m_events[i]) != VK_SUCCESS) {
            LogErrorAndExit("VulkanBackend::VulkanBackend(): could not signal event after creating it, exiting.\n");
        }
    }

    createAndSetupSwapchain(physicalDevice(), device(), m_core->surface());
    createWindowRenderTargetFrontend();

    setupDearImgui();

    m_renderGraph = std::make_unique<RenderGraph>();
    m_app.setup(*m_renderGraph);
    reconstructRenderGraphResources(*m_renderGraph);
}

VulkanBackend::~VulkanBackend()
{
    // Before destroying stuff, make sure it's done with all scheduled work
    vkDeviceWaitIdle(device());

    destroyDearImgui();

    vkFreeCommandBuffers(device(), m_renderGraphFrameCommandPool, m_frameCommandBuffers.size(), m_frameCommandBuffers.data());

    destroyRenderGraphResources();

    destroySwapchain();

    for (VkEvent event : m_events) {
        vkDestroyEvent(device(), event, nullptr);
    }

    vkDestroyCommandPool(device(), m_renderGraphFrameCommandPool, nullptr);
    vkDestroyCommandPool(device(), m_transientCommandPool, nullptr);

    for (size_t it = 0; it < maxFramesInFlight; ++it) {
        vkDestroySemaphore(device(), m_imageAvailableSemaphores[it], nullptr);
        vkDestroySemaphore(device(), m_renderFinishedSemaphores[it], nullptr);
        vkDestroyFence(device(), m_inFlightFrameFences[it], nullptr);
    }

    vmaDestroyAllocator(m_memoryAllocator);

    m_core.release();
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
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities) != VK_SUCCESS) {
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

    VkSurfaceFormatKHR surfaceFormat = m_core->pickBestSurfaceFormat();
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;

    VkPresentModeKHR presentMode = m_core->pickBestPresentMode();
    createInfo.presentMode = presentMode;

    VkExtent2D swapchainExtent = m_core->pickBestSwapchainExtent();
    createInfo.imageExtent = swapchainExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT; // TODO: What do we want here? Maybe this suffices?
    // TODO: Assure VK_IMAGE_USAGE_STORAGE_BIT is supported using vkGetPhysicalDeviceSurfaceCapabilitiesKHR & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT

    if (debugMode) {
        // for nsight debugging & similar stuff)
        createInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    uint32_t queueFamilyIndices[] = { m_graphicsQueue.familyIndex, m_presentQueue.familyIndex };
    if (!m_core->hasCombinedGraphicsComputeQueue()) {
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

    // TODO: This is clearly stupid.. again....!!
    Registry badgeGiver {};
    m_swapchainDepthTexture = Texture(badgeGiver.exchangeBadges(backendBadge()), m_swapchainExtent, Texture::Format::Depth32F, Texture::Usage::Attachment,
                                      Texture::MinFilter::Nearest, Texture::MagFilter::Nearest, Texture::Mipmap::None, Texture::Multisampling::None);
    newTexture(m_swapchainDepthTexture);
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

        if (vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, m_frameCommandBuffers.data()) != VK_SUCCESS) {
            LogErrorAndExit("VulkanBackend::createAndSetupSwapchain(): could not create the main command buffers, exiting.\n");
        }
    }
}

void VulkanBackend::destroySwapchain()
{
    destroyWindowRenderTargets();

    deleteTexture(m_swapchainDepthTexture);

    for (size_t it = 0; it < m_numSwapchainImages; ++it) {
        vkDestroyImageView(device(), m_swapchainImageViews[it], nullptr);
    }

    vkDestroySwapchainKHR(device(), m_swapchain, nullptr);
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

    vkDeviceWaitIdle(device());

    destroySwapchain();
    createAndSetupSwapchain(physicalDevice(), device(), m_core->surface());
    createWindowRenderTargetFrontend();

    s_unhandledWindowResize = false;

    return m_swapchainExtent;
}

void VulkanBackend::createWindowRenderTargetFrontend()
{
    ASSERT(m_numSwapchainImages > 0);

    // TODO: This is clearly stupid..
    Registry badgeGiver {};

    m_swapchainMockColorTextures.resize(m_numSwapchainImages);
    m_swapchainMockRenderTargets.resize(m_numSwapchainImages);

    for (size_t i = 0; i < m_numSwapchainImages; ++i) {

        TextureInfo colorInfo {};
        colorInfo.format = m_swapchainImageFormat;
        colorInfo.image = m_swapchainImages[i];
        colorInfo.view = m_swapchainImageViews[i];
        colorInfo.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (m_swapchainMockColorTextures[i].hasBackend()) {
            m_textureInfos.remove(m_swapchainMockColorTextures[i].id());
        }
        m_swapchainMockColorTextures[i] = Texture(badgeGiver.exchangeBadges(backendBadge()), m_swapchainExtent, Texture::Format::Unknown, Texture::Usage::Attachment,
                                                  Texture::MinFilter::Nearest, Texture::MagFilter::Nearest, Texture::Mipmap::None, Texture::Multisampling::None);
        size_t colorIndex = m_textureInfos.add(colorInfo);
        m_swapchainMockColorTextures[i].registerBackend(backendBadge(), colorIndex);

        RenderTargetInfo targetInfo {};
        targetInfo.compatibleRenderPass = m_swapchainRenderPass;
        targetInfo.framebuffer = m_swapchainFramebuffers[i];
        targetInfo.attachedTextures = {
            { &m_swapchainMockColorTextures[i], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR }, // this is important so that we know that we don't need to do an explicit transition before presenting
            { &m_swapchainDepthTexture, VK_IMAGE_LAYOUT_UNDEFINED } // (this probably doesn't matter for the depth image)
        };

        if (m_swapchainMockRenderTargets[i].hasBackend()) {
            m_renderTargetInfos.remove(m_swapchainMockRenderTargets[i].id());
        }
        m_swapchainMockRenderTargets[i] = RenderTarget(
            badgeGiver.exchangeBadges(backendBadge()),
            { { RenderTarget::AttachmentType::Color0, &m_swapchainMockColorTextures[i] },
              { RenderTarget::AttachmentType::Depth, &m_swapchainDepthTexture } });
        size_t targetIndex = m_renderTargetInfos.add(targetInfo);
        m_swapchainMockRenderTargets[i].registerBackend(backendBadge(), targetIndex);
    }
}

void VulkanBackend::setupDearImgui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    //ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

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
    if (vkCreateDescriptorPool(device(), &descPoolCreateInfo, nullptr, &m_guiDescriptorPool) != VK_SUCCESS) {
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

    if (vkCreateRenderPass(device(), &renderPassCreateInfo, nullptr, &m_guiRenderPass) != VK_SUCCESS) {
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

    initInfo.Instance = m_core->instance();
    initInfo.PhysicalDevice = physicalDevice();
    initInfo.Device = device();
    initInfo.Allocator = nullptr;

    initInfo.QueueFamily = m_graphicsQueue.familyIndex;
    initInfo.Queue = m_graphicsQueue.queue;

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
    vkDestroyDescriptorPool(device(), m_guiDescriptorPool, nullptr);
    vkDestroyRenderPass(device(), m_guiRenderPass, nullptr);
    for (VkFramebuffer framebuffer : m_guiFramebuffers) {
        vkDestroyFramebuffer(device(), framebuffer, nullptr);
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    m_guiIsSetup = false;
}

void VulkanBackend::updateDearImguiFramebuffers()
{
    for (VkFramebuffer& framebuffer : m_guiFramebuffers) {
        vkDestroyFramebuffer(device(), framebuffer, nullptr);
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
        if (vkCreateFramebuffer(device(), &framebufferCreateInfo, nullptr, &framebuffer) != VK_SUCCESS) {
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

    Texture& swapchainTexture = m_swapchainMockColorTextures[swapchainImageIndex];
    textureInfo(swapchainTexture).currentLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
}

bool VulkanBackend::executeFrame(double elapsedTime, double deltaTime, bool renderGui)
{
    uint32_t currentFrameMod = m_currentFrameIndex % maxFramesInFlight;

    if (vkWaitForFences(device(), 1, &m_inFlightFrameFences[currentFrameMod], VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        LogError("VulkanBackend::executeFrame(): error while waiting for in-flight frame fence (frame %u).\n", m_currentFrameIndex);
    }

    AppState appState { m_swapchainExtent, deltaTime, elapsedTime, m_currentFrameIndex };

    uint32_t swapchainImageIndex;
    VkResult acquireResult = vkAcquireNextImageKHR(device(), m_swapchain, UINT64_MAX, m_imageAvailableSemaphores[currentFrameMod], VK_NULL_HANDLE, &swapchainImageIndex);

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
    const Texture& currentColorTexture = m_swapchainMockColorTextures[swapchainImageIndex];
    textureInfo(currentColorTexture).currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    textureInfo(m_swapchainDepthTexture).currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    drawFrame(appState, elapsedTime, deltaTime, renderGui, swapchainImageIndex);

    submitQueue(swapchainImageIndex, &m_imageAvailableSemaphores[currentFrameMod], &m_renderFinishedSemaphores[currentFrameMod], &m_inFlightFrameFences[currentFrameMod]);

    // Present results (synced on the semaphores)
    {
        VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };

        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &m_renderFinishedSemaphores[currentFrameMod];

        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &m_swapchain;
        presentInfo.pImageIndices = &swapchainImageIndex;

        VkResult presentResult = vkQueuePresentKHR(m_presentQueue.queue, &presentInfo);

        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || s_unhandledWindowResize) {
            recreateSwapchain();
            reconstructRenderGraphResources(*m_renderGraph);
        } else if (presentResult != VK_SUCCESS) {
            LogError("VulkanBackend::executeFrame(): could not present swapchain (frame %u).\n", m_currentFrameIndex);
        }
    }

    m_currentFrameIndex += 1;
    return true;
}

void VulkanBackend::drawFrame(const AppState& appState, double elapsedTime, double deltaTime, bool renderGui, uint32_t swapchainImageIndex)
{
    ASSERT(m_renderGraph);

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    m_app.update(float(elapsedTime), float(deltaTime));

    VkCommandBufferBeginInfo commandBufferBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    commandBufferBeginInfo.flags = 0u;
    commandBufferBeginInfo.pInheritanceInfo = nullptr;

    VkCommandBuffer commandBuffer = m_frameCommandBuffers[swapchainImageIndex];
    if (vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo) != VK_SUCCESS) {
        LogError("VulkanBackend::executeRenderGraph(): error beginning command buffer command!\n");
    }

    Registry& associatedRegistry = *m_frameRegistries[swapchainImageIndex];
    VulkanCommandList cmdList { *this, commandBuffer };

    ImGui::Begin("Nodes");
    m_renderGraph->forEachNodeInResolvedOrder(associatedRegistry, [&](const RenderGraphNode::ExecuteCallback& nodeExecuteCallback) {
        nodeExecuteCallback(appState, cmdList);
        cmdList.endNode({});
    });
    ImGui::End();

    if (renderGui) {
        ImGui::Render();
        renderDearImguiFrame(commandBuffer, swapchainImageIndex);
    } else {
        ImGui::EndFrame();
    }
    ImGui::UpdatePlatformWindows();

    // Explicitly tranfer the swapchain image to a present layout if not already
    // In most cases it should always be, but with nsight it seems to do weird things.
    Texture& swapchainTexture = m_swapchainMockColorTextures[swapchainImageIndex];
    TextureInfo& texInfo = textureInfo(swapchainTexture);
    if (texInfo.currentLayout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        transitionImageLayout(texInfo.image, false, texInfo.currentLayout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, &commandBuffer);
        LogInfo("VulkanBackend::executeRenderGraph(): performing explicit swapchain layout transition. "
                "This should only happen if we don't render to the window and don't draw any GUI.\n");
    }

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        LogError("VulkanBackend::executeRenderGraph(): error ending command buffer command!\n");
    }
}

void VulkanBackend::newBuffer(const Buffer& buffer)
{
    // NOTE: Vulkan doesn't seem to like to create buffers of size 0. Of course, it's correct
    //  in that it is stupid, but it can be useful when debugging and testing to just not supply
    //  any data and create an empty buffer while not having to change any shader code or similar.
    //  To get around this here we simply force a size of 1 instead, but as far as the frontend
    //  is conserned we don't have access to that one byte.
    size_t bufferSize = buffer.size();
    if (bufferSize == 0) {
        bufferSize = 1;
    }

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
    case Buffer::Usage::StorageBuffer:
        usageFlags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    if (debugMode) {
        // for nsight debugging & similar stuff)
        usageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
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
    bufferCreateInfo.size = bufferSize;
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
    case Texture::Format::RGBA8:
        format = VK_FORMAT_R8G8B8A8_UNORM;
        break;
    case Texture::Format::sRGBA8:
        format = VK_FORMAT_R8G8B8A8_SRGB;
        break;
    case Texture::Format::R16F:
        format = VK_FORMAT_R16_SFLOAT;
        break;
    case Texture::Format::RGBA16F:
        format = VK_FORMAT_R16G16B16A16_SFLOAT;
        break;
    case Texture::Format::RGBA32F:
        format = VK_FORMAT_R32G32B32A32_SFLOAT;
        break;
    case Texture::Format::Depth32F:
        format = VK_FORMAT_D32_SFLOAT;
        break;
    case Texture::Format::Unknown:
        LogErrorAndExit("Trying to create new texture with format Unknown, which is not allowed!\n");
    default:
        ASSERT_NOT_REACHED();
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
    case Texture::Usage::AttachAndSample:
        usageFlags = attachmentFlags | sampledFlags;
        break;
    case Texture::Usage::StorageAndSample:
        usageFlags = VK_IMAGE_USAGE_STORAGE_BIT | sampledFlags;
    }

    // (if we later want to generate mipmaps we need the ability to use each mip as a src & dst in blitting)
    if (texture.hasMipmaps()) {
        usageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        usageFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    if (debugMode) {
        // for nsight debugging & similar stuff)
        usageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
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
    imageCreateInfo.samples = static_cast<VkSampleCountFlagBits>(texture.multisampling());
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
    if (vkCreateImageView(device(), &viewCreateInfo, nullptr, &imageView) != VK_SUCCESS) {
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
    if (vkCreateSampler(device(), &samplerCreateInfo, nullptr, &sampler) != VK_SUCCESS) {
        LogError("VulkanBackend::newTexture(): could not create sampler for the image.\n");
    }

    VkImageLayout layout;
    switch (texture.usage()) {
    case Texture::Usage::AttachAndSample:
        // We probably want to render to it before sampling from it
    case Texture::Usage::Attachment:
        layout = texture.hasDepthFormat() ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        break;
    case Texture::Usage::Sampled:
        layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        break;
    case Texture::Usage::StorageAndSample:
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
    vkDestroySampler(device(), texInfo.sampler, nullptr);
    vkDestroyImageView(device(), texInfo.view, nullptr);
    vmaDestroyImage(m_memoryAllocator, texInfo.image, texInfo.allocation);

    m_textureInfos.remove(texture.id());
    texture.unregisterBackend(backendBadge());
}

void VulkanBackend::updateTexture(const TextureUpdate& update)
{
    if (update.texture().id() == Resource::NullId) {
        LogErrorAndExit("Trying to update an already-deleted or not-yet-created texture\n");
    }

    int numChannels;
    switch (update.texture().format()) {
    case Texture::Format::RGBA8:
    case Texture::Format::sRGBA8:
    case Texture::Format::RGBA16F:
    case Texture::Format::RGBA32F:
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
    VkDeviceSize pixelsSize;

    bool isHdr = false;
    stbi_uc* pixels { nullptr };
    float* hdrPixels { nullptr };

    if (update.hasPath()) {
        if (!FileIO::isFileReadable(update.path())) {
            LogError("VulkanBackend::updateTexture(): there is no file that can be read at path '%s'.\n", update.path().c_str());
            return;
        }

        isHdr = stbi_is_hdr(update.path().c_str());

        if (isHdr) {
            hdrPixels = stbi_loadf(update.path().c_str(), &width, &height, nullptr, numChannels);
            pixelsSize = width * height * numChannels * sizeof(float);
        } else {
            pixels = stbi_load(update.path().c_str(), &width, &height, nullptr, numChannels);
            pixelsSize = width * height * numChannels * sizeof(stbi_uc);
        }

        if ((isHdr && !hdrPixels) || (!isHdr && !pixels)) {
            LogError("VulkanBackend::updateTexture(): stb_image could not read the contents of '%s'.\n", update.path().c_str());
            return;
        }

        if (Extent2D(width, height) != update.texture().extent()) {
            LogErrorAndExit("VulkanBackend::updateTexture(): loaded texture does not match specified extent.\n");
        }

    } else {
        width = 1;
        height = 1;

        vec4 color = update.pixelValue();
        pixelsSize = (width * height) * (4 * sizeof(stbi_uc));
        pixels = (stbi_uc*)malloc(pixelsSize);
        pixels[0] = (stbi_uc)(mathkit::clamp(color.r, 0.0f, 1.0f) * 255.99f);
        pixels[1] = (stbi_uc)(mathkit::clamp(color.g, 0.0f, 1.0f) * 255.99f);
        pixels[2] = (stbi_uc)(mathkit::clamp(color.b, 0.0f, 1.0f) * 255.99f);
        pixels[3] = (stbi_uc)(mathkit::clamp(color.a, 0.0f, 1.0f) * 255.99f);
    }

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

    if (!setBufferMemoryUsingMapping(stagingAllocation, isHdr ? (void*)hdrPixels : (void*)pixels, pixelsSize)) {
        LogError("VulkanBackend::updateTexture(): could set the buffer memory for the staging buffer.\n");
        return;
    }

    AT_SCOPE_EXIT([&]() {
        vmaDestroyBuffer(m_memoryAllocator, stagingBuffer, stagingAllocation);
        if (update.hasPath()) {
            stbi_image_free(pixels);
        } else {
            if (isHdr) {
                stbi_image_free(hdrPixels);
            } else {
                stbi_image_free(pixels);
            }
        }
    });

    TextureInfo& texInfo = textureInfo(update.texture());

    // NOTE: Since we are updating the texture we don't care what was in the image before. For these cases undefined
    //  works fine, since it will simply discard/ignore whatever data is in it before.
    VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (!transitionImageLayout(texInfo.image, update.texture().hasDepthFormat(), oldLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)) {
        LogError("VulkanBackend::updateTexture(): could not transition the image to transfer layout.\n");
    }
    if (!copyBufferToImage(stagingBuffer, texInfo.image, width, height, update.texture().hasDepthFormat())) {
        LogError("VulkanBackend::updateTexture(): could not copy the staging buffer to the image.\n");
    }

    VkImageLayout finalLayout;
    switch (update.texture().usage()) {
    case Texture::Usage::AttachAndSample:
        // We probably want to render to it before sampling from it
    case Texture::Usage::Attachment:
        finalLayout = update.texture().hasDepthFormat() ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        break;
    case Texture::Usage::Sampled:
        finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        break;
    case Texture::Usage::StorageAndSample:
        finalLayout = VK_IMAGE_LAYOUT_GENERAL;
        break;
    }
    texInfo.currentLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    auto extent = update.texture().extent();
    if (update.generateMipmaps() && extent.width() > 1 && extent.height() > 1) {
        generateMipmaps(update.texture(), finalLayout);
    } else {
        if (!transitionImageLayout(texInfo.image, update.texture().hasDepthFormat(), texInfo.currentLayout, finalLayout)) {
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

    for (auto& [type, texture, loadOp, storeOp] : renderTarget.sortedAttachments()) {

        // If the attachments are sorted properly (i.e. depth very last) then this should never happen!
        // This is important for the VkAttachmentReference attachment index later in this loop.
        ASSERT(!depthAttachmentRef.has_value());

        const TextureInfo& texInfo = textureInfo(*texture);

        VkAttachmentDescription attachment = {};
        attachment.format = texInfo.format;

        // TODO: Handle multisampling and stencil stuff!
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

        switch (loadOp) {
        case LoadOp::Load:
            attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;

            // TODO/FIXME: For LOAD_OP_LOAD we actually need to provide a valid initialLayout! Using texInfo.currentLayout
            //  won't work since we only use the layout at the time of creating this render pass, and not what it is in
            //  runtime. Not sure what the best way of doing this is. What about always using explicit transitions before
            //  binding this render target, and then here have the same initialLayout and finalLayout so nothing(?) happens.
            //  Could maybe work, but we have to figure out if it's actually a noop if initial & final are equal!
            attachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; //texInfo.currentLayout;
            ASSERT_NOT_REACHED();
            break;
        case LoadOp::Clear:
            attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            break;
        }

        switch (storeOp) {
        case StoreOp::Store:
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            break;
        case StoreOp::Ignore:
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            break;
        }

        if (type == RenderTarget::AttachmentType::Depth) {
            attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        } else {
            attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }

        uint32_t attachmentIndex = allAttachments.size();
        allAttachments.push_back(attachment);
        allAttachmentImageViews.push_back(texInfo.view);

        VkAttachmentReference attachmentRef = {};
        attachmentRef.attachment = attachmentIndex;
        attachmentRef.layout = attachment.finalLayout;
        if (type == RenderTarget::AttachmentType::Depth) {
            depthAttachmentRef = attachmentRef;
        } else {
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
    if (vkCreateRenderPass(device(), &renderPassCreateInfo, nullptr, &renderPass) != VK_SUCCESS) {
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
    if (vkCreateFramebuffer(device(), &framebufferCreateInfo, nullptr, &framebuffer) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create framebuffer\n");
    }

    RenderTargetInfo renderTargetInfo {};
    renderTargetInfo.compatibleRenderPass = renderPass;
    renderTargetInfo.framebuffer = framebuffer;
    for (auto& attachment : renderTarget.sortedAttachments()) {
        VkImageLayout finalLayout = (attachment.type == RenderTarget::AttachmentType::Depth)
            ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        renderTargetInfo.attachedTextures.push_back({ attachment.texture, finalLayout });
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
    vkDestroyFramebuffer(device(), targetInfo.framebuffer, nullptr);
    vkDestroyRenderPass(device(), targetInfo.compatibleRenderPass, nullptr);

    m_renderTargetInfos.remove(renderTarget.id());
    renderTarget.unregisterBackend(backendBadge());
}

void VulkanBackend::setupWindowRenderTargets()
{
    // TODO: Could this be rewritten to only use our existing newRenderTarget method?

    auto& depthTexInfo = textureInfo(m_swapchainDepthTexture);

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
    depthAttachment.format = depthTexInfo.format;
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
    if (vkCreateRenderPass(device(), &renderPassCreateInfo, nullptr, &renderPass) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create window render pass\n");
    }
    m_swapchainRenderPass = renderPass;

    m_swapchainFramebuffers.resize(m_numSwapchainImages);
    for (size_t it = 0; it < m_numSwapchainImages; ++it) {

        std::array<VkImageView, 2> attachmentImageViews = {
            m_swapchainImageViews[it],
            depthTexInfo.view
        };

        VkFramebufferCreateInfo framebufferCreateInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        framebufferCreateInfo.renderPass = renderPass;
        framebufferCreateInfo.attachmentCount = attachmentImageViews.size();
        framebufferCreateInfo.pAttachments = attachmentImageViews.data();
        framebufferCreateInfo.width = m_swapchainExtent.width();
        framebufferCreateInfo.height = m_swapchainExtent.height();
        framebufferCreateInfo.layers = 1;

        VkFramebuffer framebuffer;
        if (vkCreateFramebuffer(device(), &framebufferCreateInfo, nullptr, &framebuffer) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to create window framebuffer\n");
        }

        m_swapchainFramebuffers[it] = framebuffer;
    }
}

void VulkanBackend::destroyWindowRenderTargets()
{
    // TODO: Could this be rewritten to only use our existing deleteRenderTarget method?

    for (RenderTarget& renderTarget : m_swapchainMockRenderTargets) {
        RenderTargetInfo& info = renderTargetInfo(renderTarget);
        vkDestroyFramebuffer(device(), info.framebuffer, nullptr);
    }

    RenderTargetInfo& info = renderTargetInfo(m_swapchainMockRenderTargets[0]);
    vkDestroyRenderPass(device(), info.compatibleRenderPass, nullptr);
}

VulkanBackend::RenderTargetInfo& VulkanBackend::renderTargetInfo(const RenderTarget& renderTarget)
{
    RenderTargetInfo& renderTargetInfo = m_renderTargetInfos[renderTarget.id()];
    return renderTargetInfo;
}

VulkanBackend::BindingSetInfo& VulkanBackend::bindingSetInfo(const BindingSet& bindingSet)
{
    BindingSetInfo& bindingSetInfo = m_bindingSetInfos[bindingSet.id()];
    return bindingSetInfo;
}

void VulkanBackend::newBindingSet(const BindingSet& bindingSet)
{
    VkDescriptorSetLayout descriptorSetLayout {};
    {
        std::vector<VkDescriptorSetLayoutBinding> layoutBindings {};
        layoutBindings.reserve(bindingSet.shaderBindings().size());

        for (auto& bindingInfo : bindingSet.shaderBindings()) {

            VkDescriptorSetLayoutBinding binding = {};
            binding.binding = bindingInfo.bindingIndex;
            binding.descriptorCount = bindingInfo.count;

            switch (bindingInfo.type) {
            case ShaderBindingType::UniformBuffer:
                binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                break;
            case ShaderBindingType::StorageBuffer:
            case ShaderBindingType::StorageBufferArray:
                binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                break;
            case ShaderBindingType::StorageImage:
                binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                break;
            case ShaderBindingType::TextureSampler:
            case ShaderBindingType::TextureSamplerArray:
                binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                break;
            case ShaderBindingType::RTAccelerationStructure:
                binding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV;
                break;
            default:
                ASSERT_NOT_REACHED();
            }

            if (bindingInfo.shaderStage & ShaderStageVertex)
                binding.stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
            if (bindingInfo.shaderStage & ShaderStageFragment)
                binding.stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
            if (bindingInfo.shaderStage & ShaderStageCompute)
                binding.stageFlags |= VK_SHADER_STAGE_COMPUTE_BIT;
            if (bindingInfo.shaderStage & ShaderStageRTRayGen)
                binding.stageFlags |= VK_SHADER_STAGE_RAYGEN_BIT_NV;
            if (bindingInfo.shaderStage & ShaderStageRTMiss)
                binding.stageFlags |= VK_SHADER_STAGE_MISS_BIT_NV;
            if (bindingInfo.shaderStage & ShaderStageRTClosestHit)
                binding.stageFlags |= VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;
            if (bindingInfo.shaderStage & ShaderStageRTAnyHit)
                binding.stageFlags |= VK_SHADER_STAGE_ANY_HIT_BIT_NV;
            if (bindingInfo.shaderStage & ShaderStageRTIntersection)
                binding.stageFlags |= VK_SHADER_STAGE_INTERSECTION_BIT_NV;

            ASSERT(binding.stageFlags != 0);

            binding.pImmutableSamplers = nullptr;

            layoutBindings.push_back(binding);
        }

        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        descriptorSetLayoutCreateInfo.bindingCount = layoutBindings.size();
        descriptorSetLayoutCreateInfo.pBindings = layoutBindings.data();

        if (vkCreateDescriptorSetLayout(device(), &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to create descriptor set layout\n");
        }
    }

    VkDescriptorPool descriptorPool {};
    {
        // TODO: Maybe in the future we don't want one pool per shader binding state? We could group a lot of stuff together probably..?

        std::unordered_map<ShaderBindingType, size_t> bindingTypeIndex {};
        std::vector<VkDescriptorPoolSize> descriptorPoolSizes {};

        for (auto& bindingInfo : bindingSet.shaderBindings()) {

            ShaderBindingType type = bindingInfo.type;

            auto entry = bindingTypeIndex.find(type);
            if (entry == bindingTypeIndex.end()) {

                VkDescriptorPoolSize poolSize = {};
                poolSize.descriptorCount = bindingInfo.count;

                switch (bindingInfo.type) {
                case ShaderBindingType::UniformBuffer:
                    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    break;
                case ShaderBindingType::StorageBuffer:
                case ShaderBindingType::StorageBufferArray:
                    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    break;
                case ShaderBindingType::StorageImage:
                    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                    break;
                case ShaderBindingType::TextureSampler:
                case ShaderBindingType::TextureSamplerArray:
                    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    break;
                case ShaderBindingType::RTAccelerationStructure:
                    poolSize.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV;
                    break;
                default:
                    ASSERT_NOT_REACHED();
                }

                bindingTypeIndex[type] = descriptorPoolSizes.size();
                descriptorPoolSizes.push_back(poolSize);

            } else {

                size_t index = entry->second;
                VkDescriptorPoolSize& poolSize = descriptorPoolSizes[index];
                poolSize.descriptorCount += bindingInfo.count;
            }
        }

        VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        descriptorPoolCreateInfo.poolSizeCount = descriptorPoolSizes.size();
        descriptorPoolCreateInfo.pPoolSizes = descriptorPoolSizes.data();
        descriptorPoolCreateInfo.maxSets = 1;

        if (vkCreateDescriptorPool(device(), &descriptorPoolCreateInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to create descriptor pool\n");
        }
    }

    VkDescriptorSet descriptorSet {};
    {
        VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        descriptorSetAllocateInfo.descriptorPool = descriptorPool;
        descriptorSetAllocateInfo.descriptorSetCount = 1;
        descriptorSetAllocateInfo.pSetLayouts = &descriptorSetLayout;

        if (vkAllocateDescriptorSets(device(), &descriptorSetAllocateInfo, &descriptorSet) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to create descriptor set\n");
        }
    }

    // Update descriptor set
    {
        std::vector<VkWriteDescriptorSet> descriptorSetWrites {};
        CapList<VkDescriptorBufferInfo> descBufferInfos { 1024 };
        CapList<VkDescriptorImageInfo> descImageInfos { 1024 };
        std::optional<VkWriteDescriptorSetAccelerationStructureNV> accelStructWrite {};

        for (auto& bindingInfo : bindingSet.shaderBindings()) {

            VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            write.pTexelBufferView = nullptr;

            write.dstSet = descriptorSet;
            write.dstBinding = bindingInfo.bindingIndex;

            switch (bindingInfo.type) {
            case ShaderBindingType::UniformBuffer: {

                ASSERT(bindingInfo.buffers.size() == 1);
                ASSERT(bindingInfo.buffers[0]);
                const BufferInfo& bufInfo = bufferInfo(*bindingInfo.buffers[0]);

                VkDescriptorBufferInfo descBufferInfo {};
                descBufferInfo.offset = 0;
                descBufferInfo.range = VK_WHOLE_SIZE;
                descBufferInfo.buffer = bufInfo.buffer;

                descBufferInfos.push_back(descBufferInfo);
                write.pBufferInfo = &descBufferInfos.back();
                write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

                write.descriptorCount = 1;
                write.dstArrayElement = 0;

                break;
            }

            case ShaderBindingType::StorageBuffer: {

                ASSERT(bindingInfo.buffers.size() == 1);
                ASSERT(bindingInfo.buffers[0]);
                const BufferInfo& bufInfo = bufferInfo(*bindingInfo.buffers[0]);

                VkDescriptorBufferInfo descBufferInfo {};
                descBufferInfo.offset = 0;
                descBufferInfo.range = VK_WHOLE_SIZE;
                descBufferInfo.buffer = bufInfo.buffer;

                descBufferInfos.push_back(descBufferInfo);
                write.pBufferInfo = &descBufferInfos.back();
                write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

                write.descriptorCount = 1;
                write.dstArrayElement = 0;

                break;
            }

            case ShaderBindingType::StorageBufferArray: {

                ASSERT(bindingInfo.count == bindingInfo.buffers.size());

                if (bindingInfo.count == 0) {
                    continue;
                }

                for (const Buffer* buffer : bindingInfo.buffers) {

                    ASSERT(buffer);
                    ASSERT(buffer->usage() == Buffer::Usage::StorageBuffer);
                    const BufferInfo& bufInfo = bufferInfo(*buffer);

                    VkDescriptorBufferInfo descBufferInfo {};
                    descBufferInfo.offset = 0;
                    descBufferInfo.range = VK_WHOLE_SIZE;
                    descBufferInfo.buffer = bufInfo.buffer;

                    descBufferInfos.push_back(descBufferInfo);
                }

                // NOTE: This should point at the first VkDescriptorBufferInfo
                write.pBufferInfo = &descBufferInfos.back() - (bindingInfo.count - 1);
                write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                write.descriptorCount = bindingInfo.count;
                write.dstArrayElement = 0;

                break;
            }

            case ShaderBindingType::StorageImage: {

                ASSERT(bindingInfo.textures.size() == 1);
                ASSERT(bindingInfo.textures[0]);

                const Texture& texture = *bindingInfo.textures[0];
                const TextureInfo& texInfo = textureInfo(texture);

                VkDescriptorImageInfo descImageInfo {};
                descImageInfo.sampler = texInfo.sampler;
                descImageInfo.imageView = texInfo.view;

                ASSERT(texture.usage() == Texture::Usage::StorageAndSample);
                descImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

                descImageInfos.push_back(descImageInfo);
                write.pImageInfo = &descImageInfos.back();
                write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

                write.descriptorCount = 1;
                write.dstArrayElement = 0;

                break;
            }

            case ShaderBindingType::TextureSampler: {

                ASSERT(bindingInfo.textures.size() == 1);
                ASSERT(bindingInfo.textures[0]);

                const Texture& texture = *bindingInfo.textures[0];
                const TextureInfo& texInfo = textureInfo(texture);

                VkDescriptorImageInfo descImageInfo {};
                descImageInfo.sampler = texInfo.sampler;
                descImageInfo.imageView = texInfo.view;

                //ASSERT(texInfo.currentLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                ASSERT(texture.usage() == Texture::Usage::Sampled || texture.usage() == Texture::Usage::AttachAndSample || texture.usage() == Texture::Usage::StorageAndSample);
                descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; //texInfo.currentLayout;

                descImageInfos.push_back(descImageInfo);
                write.pImageInfo = &descImageInfos.back();
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

                write.descriptorCount = 1;
                write.dstArrayElement = 0;

                break;
            }

            case ShaderBindingType::TextureSamplerArray: {

                size_t numTextures = bindingInfo.textures.size();
                ASSERT(numTextures > 0);

                for (uint32_t i = 0; i < bindingInfo.count; ++i) {

                    // NOTE: We always have to fill in the count here, but for the unused we just fill with a "default"
                    const Texture* texture = (i >= numTextures) ? bindingInfo.textures.front() : bindingInfo.textures[i];

                    ASSERT(texture);
                    const TextureInfo& texInfo = textureInfo(*texture);

                    VkDescriptorImageInfo descImageInfo {};
                    descImageInfo.sampler = texInfo.sampler;
                    descImageInfo.imageView = texInfo.view;

                    ASSERT(texture->usage() == Texture::Usage::Sampled || texture->usage() == Texture::Usage::AttachAndSample);
                    descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                    descImageInfos.push_back(descImageInfo);
                }

                // NOTE: This should point at the first VkDescriptorImageInfo
                write.pImageInfo = &descImageInfos.back() - (bindingInfo.count - 1);
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.descriptorCount = bindingInfo.count;
                write.dstArrayElement = 0;

                break;
            }

            case ShaderBindingType::RTAccelerationStructure: {

                ASSERT(bindingInfo.textures.empty());
                ASSERT(bindingInfo.buffers.empty());
                ASSERT(bindingInfo.tlas != nullptr);

                const TopLevelAS& tlas = *bindingInfo.tlas;
                auto& tlasInfo = accelerationStructureInfo(tlas);

                VkWriteDescriptorSetAccelerationStructureNV descriptorAccelerationStructureInfo { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_NV };
                descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
                descriptorAccelerationStructureInfo.pAccelerationStructures = &tlasInfo.accelerationStructure;

                // (there can only be one in a set!) (well maybe not, but it makes sense..)
                ASSERT(!accelStructWrite.has_value());
                accelStructWrite = descriptorAccelerationStructureInfo;

                write.pNext = &accelStructWrite.value();
                write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV;

                write.descriptorCount = 1;
                write.dstArrayElement = 0;

                break;
            }

            default:
                ASSERT_NOT_REACHED();
            }

            descriptorSetWrites.push_back(write);
        }

        vkUpdateDescriptorSets(device(), descriptorSetWrites.size(), descriptorSetWrites.data(), 0, nullptr);
    }

    BindingSetInfo info {};
    info.descriptorPool = descriptorPool;
    info.descriptorSetLayout = descriptorSetLayout;
    info.descriptorSet = descriptorSet;

    size_t index = m_bindingSetInfos.add(info);
    bindingSet.registerBackend(backendBadge(), index);
}

void VulkanBackend::deleteBindingSet(const BindingSet& bindingSet)
{
    if (bindingSet.id() == Resource::NullId) {
        LogErrorAndExit("Trying to delete an already-deleted or not-yet-created shader binding set\n");
    }

    BindingSetInfo& setInfo = bindingSetInfo(bindingSet);
    vkDestroyDescriptorPool(device(), setInfo.descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device(), setInfo.descriptorSetLayout, nullptr);

    m_bindingSetInfos.remove(bindingSet.id());
    bindingSet.unregisterBackend(backendBadge());
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
            const std::vector<uint32_t>& spirv = ShaderManager::instance().spirv(file.path());
            moduleCreateInfo.codeSize = sizeof(uint32_t) * spirv.size();
            moduleCreateInfo.pCode = spirv.data();

            VkShaderModule shaderModule {};
            if (vkCreateShaderModule(device(), &moduleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS) {
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
                stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                break;
            }
            stageCreateInfo.stage = stageFlags;

            shaderStages.push_back(stageCreateInfo);
        }
    }

    //
    // Create pipeline layout
    //
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };

    const auto& [descriptorSetLayouts, pushConstantRange] = createDescriptorSetLayoutForShader(renderState.shader());

    pipelineLayoutCreateInfo.setLayoutCount = descriptorSetLayouts.size();
    pipelineLayoutCreateInfo.pSetLayouts = descriptorSetLayouts.data();

    if (pushConstantRange.has_value()) {
        pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
        pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange.value();
    } else {
        pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
        pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;
    }

    VkPipelineLayout pipelineLayout {};
    if (vkCreatePipelineLayout(device(), &pipelineLayoutCreateInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create pipeline layout\n");
    }

    // (it's *probably* safe to delete these after creating the pipeline layout! no layers are complaining)
    for (const VkDescriptorSetLayout& layout : descriptorSetLayouts) {
        vkDestroyDescriptorSetLayout(device(), layout, nullptr);
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

    VkPipelineColorBlendStateCreateInfo colorBlending = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments {};
    if (renderState.blendState().enabled) {
        // TODO: Implement blending!
        ASSERT_NOT_REACHED();
    } else {
        renderState.renderTarget().forEachColorAttachment([&](const RenderTarget::Attachment& attachment) {
            VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
            colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT; // NOLINT(hicpp-signed-bitwise)
            colorBlendAttachment.blendEnable = VK_FALSE;
            colorBlendAttachments.push_back(colorBlendAttachment);
        });
    }
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = colorBlendAttachments.size();
    colorBlending.pAttachments = colorBlendAttachments.data();

    const DepthState& depthState = renderState.depthState();

    VkPipelineDepthStencilStateCreateInfo depthStencilState = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depthStencilState.depthTestEnable = depthState.testDepth;
    depthStencilState.depthWriteEnable = depthState.writeDepth;
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
    if (vkCreateGraphicsPipelines(device(), VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create graphics pipeline\n");
    }

    // Remove shader modules, they are no longer needed after creating the pipeline
    for (auto& stage : shaderStages) {
        vkDestroyShaderModule(device(), stage.module, nullptr);
    }

    RenderStateInfo renderStateInfo {};
    renderStateInfo.pipelineLayout = pipelineLayout;
    renderStateInfo.pipeline = graphicsPipeline;

    for (auto& set : renderState.bindingSets()) {
        for (auto& bindingInfo : set->shaderBindings()) {
            for (auto texture : bindingInfo.textures) {
                renderStateInfo.sampledTextures.push_back(texture);
            }
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
    vkDestroyPipeline(device(), stateInfo.pipeline, nullptr);
    vkDestroyPipelineLayout(device(), stateInfo.pipelineLayout, nullptr);

    m_renderStateInfos.remove(renderState.id());
    renderState.unregisterBackend(backendBadge());
}

void VulkanBackend::newBottomLevelAccelerationStructure(const BottomLevelAS& blas)
{
    if (!m_rtx.has_value()) {
        LogErrorAndExit("Trying to create a bottom level acceleration structure, but there is no ray tracing support!\n");
    }

    // All geometries in a BLAS must have the same type (i.e. AABB/triangles)
    bool isTriangleBLAS = blas.geometries().front().hasTriangles();
    for (size_t i = 1; i < blas.geometries().size(); ++i) {
        ASSERT(blas.geometries()[i].hasTriangles() == isTriangleBLAS);
    }

    VkBuffer transformBuffer;
    VmaAllocation transformBufferAllocation;
    size_t singleTransformSize = 3 * 4 * sizeof(float);
    if (isTriangleBLAS) {
        std::vector<glm::mat3x4> transforms {};
        for (auto& geo : blas.geometries()) {
            glm::mat3x4 mat34 = transpose(geo.triangles().transform);
            transforms.push_back(mat34);
        }

        size_t totalSize = transforms.size() * singleTransformSize;

        VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferCreateInfo.usage = VK_BUFFER_USAGE_RAY_TRACING_BIT_NV; // (I can't find info on usage from the spec, but I assume this should work)
        bufferCreateInfo.size = totalSize;

        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

        if (vmaCreateBuffer(m_memoryAllocator, &bufferCreateInfo, &allocCreateInfo, &transformBuffer, &transformBufferAllocation, nullptr) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to create buffer for the bottom level acceeration structure transforms.\n");
        }

        if (!setBufferMemoryUsingMapping(transformBufferAllocation, transforms.data(), totalSize)) {
            LogErrorAndExit("Error trying to copy data to the bottom level acceeration structure transform buffer.\n");
        }
    }

    std::vector<VkGeometryNV> geometries {};

    for (size_t geoIdx = 0; geoIdx < blas.geometries().size(); ++geoIdx) {
        const RTGeometry& geo = blas.geometries()[geoIdx];

        if (geo.hasTriangles()) {
            const RTTriangleGeometry& triGeo = geo.triangles();

            VkGeometryTrianglesNV triangles { VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV };

            triangles.vertexData = bufferInfo(triGeo.vertexBuffer).buffer;
            triangles.vertexOffset = 0;
            triangles.vertexStride = triGeo.vertexStride;
            triangles.vertexCount = triGeo.vertexBuffer.size() / triangles.vertexStride;
            switch (triGeo.vertexFormat) {
            case VertexFormat::XYZ32F:
                triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
                break;
            }

            triangles.indexData = bufferInfo(triGeo.indexBuffer).buffer;
            triangles.indexOffset = 0;
            switch (triGeo.indexType) {
            case IndexType::UInt16:
                triangles.indexType = VK_INDEX_TYPE_UINT16;
                triangles.indexCount = triGeo.indexBuffer.size() / sizeof(uint16_t);
                break;
            case IndexType::UInt32:
                triangles.indexType = VK_INDEX_TYPE_UINT32;
                triangles.indexCount = triGeo.indexBuffer.size() / sizeof(uint32_t);
                break;
            }

            triangles.transformData = transformBuffer;
            triangles.transformOffset = geoIdx * singleTransformSize;

            VkGeometryNV geometry { VK_STRUCTURE_TYPE_GEOMETRY_NV };
            geometry.flags = VK_GEOMETRY_OPAQUE_BIT_NV; // "indicates that this geometry does not invoke the any-hit shaders even if present in a hit group."

            geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_NV;
            geometry.geometry.triangles = triangles;

            VkGeometryAABBNV aabbs { VK_STRUCTURE_TYPE_GEOMETRY_AABB_NV };
            aabbs.numAABBs = 0;
            geometry.geometry.aabbs = aabbs;

            geometries.push_back(geometry);
        }

        else if (geo.hasAABBs()) {
            const RTAABBGeometry& aabbGeo = geo.aabbs();

            VkGeometryAABBNV aabbs { VK_STRUCTURE_TYPE_GEOMETRY_AABB_NV };
            aabbs.offset = 0;
            aabbs.stride = aabbGeo.aabbStride;
            aabbs.aabbData = bufferInfo(aabbGeo.aabbBuffer).buffer;
            aabbs.numAABBs = aabbGeo.aabbBuffer.size() / aabbGeo.aabbStride;

            VkGeometryNV geometry { VK_STRUCTURE_TYPE_GEOMETRY_NV };
            geometry.flags = VK_GEOMETRY_OPAQUE_BIT_NV; // "indicates that this geometry does not invoke the any-hit shaders even if present in a hit group."

            geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_NV;
            geometry.geometry.aabbs = aabbs;

            VkGeometryTrianglesNV triangles { VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV };
            triangles.vertexCount = 0;
            //triangles.indexCount = 0;
            geometry.geometry.triangles = triangles;

            geometries.push_back(geometry);
        }
    }

    VkAccelerationStructureInfoNV accelerationStructureInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV };
    accelerationStructureInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV;
    accelerationStructureInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_NV;
    accelerationStructureInfo.instanceCount = 0;
    accelerationStructureInfo.geometryCount = geometries.size();
    accelerationStructureInfo.pGeometries = geometries.data();

    VkAccelerationStructureCreateInfoNV accelerationStructureCreateInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV };
    accelerationStructureCreateInfo.info = accelerationStructureInfo;
    VkAccelerationStructureNV accelerationStructure;
    if (m_rtx->vkCreateAccelerationStructureNV(device(), &accelerationStructureCreateInfo, nullptr, &accelerationStructure) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create bottom level acceleration structure\n");
    }

    VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV };
    memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV;
    memoryRequirementsInfo.accelerationStructure = accelerationStructure;
    VkMemoryRequirements2 memoryRequirements2 {};
    m_rtx->vkGetAccelerationStructureMemoryRequirementsNV(device(), &memoryRequirementsInfo, &memoryRequirements2);

    VkMemoryAllocateInfo memoryAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    memoryAllocateInfo.allocationSize = memoryRequirements2.memoryRequirements.size;
    memoryAllocateInfo.memoryTypeIndex = findAppropriateMemory(memoryRequirements2.memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory memory;
    if (vkAllocateMemory(device(), &memoryAllocateInfo, nullptr, &memory) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create allocate memory for acceleration structure\n");
    }

    VkBindAccelerationStructureMemoryInfoNV accelerationStructureMemoryInfo { VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV };
    accelerationStructureMemoryInfo.accelerationStructure = accelerationStructure;
    accelerationStructureMemoryInfo.memory = memory;
    if (m_rtx->vkBindAccelerationStructureMemoryNV(device(), 1, &accelerationStructureMemoryInfo) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to bind memory to acceleration structure\n");
    }

    uint64_t handle { 0 };
    if (m_rtx->vkGetAccelerationStructureHandleNV(device(), accelerationStructure, sizeof(uint64_t), &handle) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to get acceleration structure handle\n");
    }

    VmaAllocation scratchAllocation;
    VkBuffer scratchBuffer = createScratchBufferForAccelerationStructure(accelerationStructure, false, scratchAllocation);

    VkAccelerationStructureInfoNV buildInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV };
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_NV;
    buildInfo.geometryCount = geometries.size();
    buildInfo.pGeometries = geometries.data();

    this->issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
        m_rtx->vkCmdBuildAccelerationStructureNV(
            commandBuffer,
            &buildInfo,
            VK_NULL_HANDLE, 0,
            VK_FALSE,
            accelerationStructure,
            VK_NULL_HANDLE,
            scratchBuffer, 0);
    });

    vmaDestroyBuffer(m_memoryAllocator, scratchBuffer, scratchAllocation);

    AccelerationStructureInfo info {};
    info.accelerationStructure = accelerationStructure;
    info.memory = memory;
    info.handle = handle;

    if (isTriangleBLAS) {
        // (should persist for the lifetime of this BLAS)
        info.associatedBuffers.push_back({ transformBuffer, transformBufferAllocation });
    }

    size_t index = m_accStructInfos.add(info);
    blas.registerBackend(backendBadge(), index);
}

void VulkanBackend::deleteBottomLevelAccelerationStructure(const BottomLevelAS& blas)
{
    if (blas.id() == Resource::NullId) {
        LogErrorAndExit("Trying to delete an already-deleted or not-yet-created bottom level acceleration structure\n");
    }

    AccelerationStructureInfo& blasInfo = accelerationStructureInfo(blas);
    m_rtx->vkDestroyAccelerationStructureNV(device(), blasInfo.accelerationStructure, nullptr);
    vkFreeMemory(device(), blasInfo.memory, nullptr);

    for (auto& [buffer, allocation] : blasInfo.associatedBuffers) {
        vmaDestroyBuffer(m_memoryAllocator, buffer, allocation);
    }

    m_accStructInfos.remove(blas.id());
    blas.unregisterBackend(backendBadge());
}

void VulkanBackend::newTopLevelAccelerationStructure(const TopLevelAS& tlas)
{
    if (!m_rtx.has_value()) {
        LogErrorAndExit("Trying to create a bottom level acceleration structure, but there is no ray tracing support!\n");
    }

    // Something more here maybe? Like fast to build/traverse, can be compacted, etc.
    auto flags = VkBuildAccelerationStructureFlagBitsNV(VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_NV | VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_NV);

    VkAccelerationStructureInfoNV accelerationStructureInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV };
    accelerationStructureInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
    accelerationStructureInfo.flags = flags;
    accelerationStructureInfo.instanceCount = tlas.instanceCount();
    accelerationStructureInfo.geometryCount = 0;

    VkAccelerationStructureCreateInfoNV accelerationStructureCreateInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV };
    accelerationStructureCreateInfo.info = accelerationStructureInfo;
    VkAccelerationStructureNV accelerationStructure;
    if (m_rtx->vkCreateAccelerationStructureNV(device(), &accelerationStructureCreateInfo, nullptr, &accelerationStructure) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create top level acceleration structure\n");
    }

    VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV };
    memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV;
    memoryRequirementsInfo.accelerationStructure = accelerationStructure;
    VkMemoryRequirements2 memoryRequirements2 {};
    m_rtx->vkGetAccelerationStructureMemoryRequirementsNV(device(), &memoryRequirementsInfo, &memoryRequirements2);

    VkMemoryAllocateInfo memoryAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    memoryAllocateInfo.allocationSize = memoryRequirements2.memoryRequirements.size;
    memoryAllocateInfo.memoryTypeIndex = findAppropriateMemory(memoryRequirements2.memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory memory;
    if (vkAllocateMemory(device(), &memoryAllocateInfo, nullptr, &memory) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create allocate memory for acceleration structure\n");
    }

    VkBindAccelerationStructureMemoryInfoNV accelerationStructureMemoryInfo { VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV };
    accelerationStructureMemoryInfo.accelerationStructure = accelerationStructure;
    accelerationStructureMemoryInfo.memory = memory;
    if (m_rtx->vkBindAccelerationStructureMemoryNV(device(), 1, &accelerationStructureMemoryInfo) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to bind memory to acceleration structure\n");
    }

    uint64_t handle { 0 };
    if (m_rtx->vkGetAccelerationStructureHandleNV(device(), accelerationStructure, sizeof(uint64_t), &handle) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to get acceleration structure handle\n");
    }

    VmaAllocation scratchAllocation;
    VkBuffer scratchBuffer = createScratchBufferForAccelerationStructure(accelerationStructure, false, scratchAllocation);

    VmaAllocation instanceAllocation;
    VkBuffer instanceBuffer = createRTXInstanceBuffer(tlas.instances(), instanceAllocation);

    VkAccelerationStructureInfoNV buildInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV };
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
    buildInfo.flags = flags;
    buildInfo.instanceCount = tlas.instanceCount();
    buildInfo.geometryCount = 0;
    buildInfo.pGeometries = nullptr;

    this->issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
        m_rtx->vkCmdBuildAccelerationStructureNV(
            commandBuffer,
            &buildInfo,
            instanceBuffer, 0,
            VK_FALSE,
            accelerationStructure,
            VK_NULL_HANDLE,
            scratchBuffer, 0);
    });

    vmaDestroyBuffer(m_memoryAllocator, scratchBuffer, scratchAllocation);

    AccelerationStructureInfo info {};
    info.accelerationStructure = accelerationStructure;
    info.memory = memory;
    info.handle = handle;

    // (should persist for the lifetime of this TLAS)
    info.associatedBuffers.push_back({ instanceBuffer, instanceAllocation });

    size_t index = m_accStructInfos.add(info);
    tlas.registerBackend(backendBadge(), index);
}

void VulkanBackend::deleteTopLevelAccelerationStructure(const TopLevelAS& tlas)
{
    if (tlas.id() == Resource::NullId) {
        LogErrorAndExit("Trying to delete an already-deleted or not-yet-created top level acceleration structure\n");
    }

    AccelerationStructureInfo& tlasInfo = accelerationStructureInfo(tlas);
    m_rtx->vkDestroyAccelerationStructureNV(device(), tlasInfo.accelerationStructure, nullptr);
    vkFreeMemory(device(), tlasInfo.memory, nullptr);

    for (auto& [buffer, allocation] : tlasInfo.associatedBuffers) {
        vmaDestroyBuffer(m_memoryAllocator, buffer, allocation);
    }

    m_accStructInfos.remove(tlas.id());
    tlas.unregisterBackend(backendBadge());
}

void VulkanBackend::newRayTracingState(const RayTracingState& rtState)
{
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts {};
    for (auto& set : rtState.bindingSets()) {
        BindingSetInfo bindingInfo = bindingSetInfo(*set);
        descriptorSetLayouts.push_back(bindingInfo.descriptorSetLayout);
    }

    // TODO: Really, it makes sense to use the descriptor set layouts we get from the helper function as well.
    //  However, the problem is that we have dynamic-length storage buffers in our ray tracing shaders, and if
    //  I'm not mistaken we need to specify the length of them in the layout. The passed in stuff should include
    //  the actual array so we know the length in that case. Without the input data though we don't know that.
    //  We will have to think about what the best way to handle that would be..
    Shader shader { rtState.shaderBindingTable().allReferencedShaderFiles(), ShaderType::RayTrace };
    const auto& [_, pushConstantRange] = createDescriptorSetLayoutForShader(shader);

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };

    pipelineLayoutCreateInfo.setLayoutCount = descriptorSetLayouts.size();
    pipelineLayoutCreateInfo.pSetLayouts = descriptorSetLayouts.data();

    if (pushConstantRange.has_value()) {
        pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
        pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange.value();
    } else {
        pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
        pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;
    }

    VkPipelineLayout pipelineLayout {};
    if (vkCreatePipelineLayout(device(), &pipelineLayoutCreateInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create pipeline layout for ray tracing\n");
    }

    const ShaderBindingTable& sbt = rtState.shaderBindingTable();
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages {};
    std::vector<VkRayTracingShaderGroupCreateInfoNV> shaderGroups {};

    // RayGen
    {
        VkShaderModuleCreateInfo moduleCreateInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        const std::vector<uint32_t>& spirv = ShaderManager::instance().spirv(sbt.rayGen().path());
        moduleCreateInfo.codeSize = sizeof(uint32_t) * spirv.size();
        moduleCreateInfo.pCode = spirv.data();

        // FIXME: this module is currently leaked!
        VkShaderModule shaderModule {};
        if (vkCreateShaderModule(device(), &moduleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to create shader module for raygen shader for ray tracing state\n");
        }

        VkPipelineShaderStageCreateInfo stageCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stageCreateInfo.stage = VK_SHADER_STAGE_RAYGEN_BIT_NV;
        stageCreateInfo.module = shaderModule;
        stageCreateInfo.pName = "main";

        uint32_t shaderIndex = shaderStages.size();
        shaderStages.push_back(stageCreateInfo);

        VkRayTracingShaderGroupCreateInfoNV shaderGroup = { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV };
        shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV;
        shaderGroup.generalShader = shaderIndex;

        shaderGroup.closestHitShader = VK_SHADER_UNUSED_NV;
        shaderGroup.anyHitShader = VK_SHADER_UNUSED_NV;
        shaderGroup.intersectionShader = VK_SHADER_UNUSED_NV;

        shaderGroups.push_back(shaderGroup);
    }

    // HitGroups
    for (const HitGroup& hitGroup : sbt.hitGroups()) {

        VkRayTracingShaderGroupCreateInfoNV shaderGroup = { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV };

        shaderGroup.type = hitGroup.hasIntersectionShader()
            ? VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_NV
            : VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_NV;

        shaderGroup.generalShader = VK_SHADER_UNUSED_NV;
        shaderGroup.closestHitShader = VK_SHADER_UNUSED_NV;
        shaderGroup.anyHitShader = VK_SHADER_UNUSED_NV;
        shaderGroup.intersectionShader = VK_SHADER_UNUSED_NV;

        // ClosestHit
        {
            VkShaderModuleCreateInfo moduleCreateInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            const std::vector<uint32_t>& spirv = ShaderManager::instance().spirv(hitGroup.closestHit().path());
            moduleCreateInfo.codeSize = sizeof(uint32_t) * spirv.size();
            moduleCreateInfo.pCode = spirv.data();

            // FIXME: this module is currently leaked!
            VkShaderModule shaderModule {};
            if (vkCreateShaderModule(device(), &moduleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS) {
                LogErrorAndExit("Error trying to create shader module for closest hit shader for ray tracing state\n");
            }

            VkPipelineShaderStageCreateInfo stageCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            stageCreateInfo.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;
            stageCreateInfo.module = shaderModule;
            stageCreateInfo.pName = "main";

            shaderGroup.closestHitShader = shaderStages.size();
            shaderStages.push_back(stageCreateInfo);
        }

        ASSERT(!hitGroup.hasAnyHitShader()); // for now!

        if (hitGroup.hasIntersectionShader()) {
            VkShaderModuleCreateInfo moduleCreateInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            const std::vector<uint32_t>& spirv = ShaderManager::instance().spirv(hitGroup.intersection().path());
            moduleCreateInfo.codeSize = sizeof(uint32_t) * spirv.size();
            moduleCreateInfo.pCode = spirv.data();

            // FIXME: this module is currently leaked!
            VkShaderModule shaderModule {};
            if (vkCreateShaderModule(device(), &moduleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS) {
                LogErrorAndExit("Error trying to create shader module for intersection shader for ray tracing state\n");
            }

            VkPipelineShaderStageCreateInfo stageCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            stageCreateInfo.stage = VK_SHADER_STAGE_INTERSECTION_BIT_NV;
            stageCreateInfo.module = shaderModule;
            stageCreateInfo.pName = "main";

            shaderGroup.intersectionShader = shaderStages.size();
            shaderStages.push_back(stageCreateInfo);
        }

        shaderGroups.push_back(shaderGroup);
    }

    // Miss shaders
    for (const ShaderFile& missShader : sbt.missShaders()) {

        VkShaderModuleCreateInfo moduleCreateInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        const std::vector<uint32_t>& spirv = ShaderManager::instance().spirv(missShader.path());
        moduleCreateInfo.codeSize = sizeof(uint32_t) * spirv.size();
        moduleCreateInfo.pCode = spirv.data();

        // FIXME: this module is currently leaked!
        VkShaderModule shaderModule {};
        if (vkCreateShaderModule(device(), &moduleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to create shader module for miss shader for ray tracing state\n");
        }

        VkPipelineShaderStageCreateInfo stageCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stageCreateInfo.stage = VK_SHADER_STAGE_MISS_BIT_NV;
        stageCreateInfo.module = shaderModule;
        stageCreateInfo.pName = "main";

        uint32_t shaderIndex = shaderStages.size();
        shaderStages.push_back(stageCreateInfo);

        VkRayTracingShaderGroupCreateInfoNV shaderGroup = { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV };
        shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV;
        shaderGroup.generalShader = shaderIndex;

        shaderGroup.closestHitShader = VK_SHADER_UNUSED_NV;
        shaderGroup.anyHitShader = VK_SHADER_UNUSED_NV;
        shaderGroup.intersectionShader = VK_SHADER_UNUSED_NV;

        shaderGroups.push_back(shaderGroup);
    }

    VkRayTracingPipelineCreateInfoNV rtPipelineCreateInfo { VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_NV };
    rtPipelineCreateInfo.maxRecursionDepth = rtState.maxRecursionDepth();
    rtPipelineCreateInfo.stageCount = shaderStages.size();
    rtPipelineCreateInfo.pStages = shaderStages.data();
    rtPipelineCreateInfo.groupCount = shaderGroups.size();
    rtPipelineCreateInfo.pGroups = shaderGroups.data();
    rtPipelineCreateInfo.layout = pipelineLayout;

    VkPipeline pipeline {};
    if (m_rtx->vkCreateRayTracingPipelinesNV(device(), VK_NULL_HANDLE, 1, &rtPipelineCreateInfo, nullptr, &pipeline) != VK_SUCCESS) {
        LogErrorAndExit("Error creating ray tracing pipeline\n");
    }

    // Create buffer for the shader binding table
    VkBuffer sbtBuffer;
    VmaAllocation sbtBufferAllocation;
    {
        uint32_t sizeOfSingleHandle = m_rtx->properties().shaderGroupHandleSize;
        uint32_t sizeOfAllHandles = sizeOfSingleHandle * shaderGroups.size();
        std::vector<std::byte> shaderGroupHandles { sizeOfAllHandles };
        if (m_rtx->vkGetRayTracingShaderGroupHandlesNV(device(), pipeline, 0, shaderGroups.size(), sizeOfAllHandles, shaderGroupHandles.data()) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to get shader group handles for the shader binding table.\n");
        }

        // TODO: For now we don't have any data, only shader handles, but we still have to consider the alignments & strides
        uint32_t baseAlignment = m_rtx->properties().shaderGroupBaseAlignment;
        uint32_t sbtSize = baseAlignment * shaderGroups.size();
        std::vector<std::byte> sbtData { sbtSize };

        for (size_t i = 0; i < shaderGroups.size(); ++i) {

            uint32_t srcOffset = i * sizeOfSingleHandle;
            uint32_t dstOffset = i * baseAlignment;

            std::copy(shaderGroupHandles.begin() + srcOffset,
                      shaderGroupHandles.begin() + srcOffset + sizeOfSingleHandle,
                      sbtData.begin() + dstOffset);
        }

        VkBufferCreateInfo sbtBufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        sbtBufferCreateInfo.usage = VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;
        sbtBufferCreateInfo.size = sbtSize;

        if (debugMode) {
            // for nsight debugging & similar stuff)
            sbtBufferCreateInfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        }

        VmaAllocationCreateInfo sbtAllocCreateInfo = {};
        sbtAllocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU; // Gpu only is probably perfectly fine, except we need to copy the data using a staging buffer

        if (vmaCreateBuffer(m_memoryAllocator, &sbtBufferCreateInfo, &sbtAllocCreateInfo, &sbtBuffer, &sbtBufferAllocation, nullptr) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to create buffer for the shader binding table.\n");
        }

        if (!setBufferMemoryUsingMapping(sbtBufferAllocation, sbtData.data(), sbtSize)) {
            LogErrorAndExit("Error trying to copy data to the shader binding table.\n");
        }
    }

    RayTracingStateInfo rtStateInfo {};
    rtStateInfo.pipelineLayout = pipelineLayout;
    rtStateInfo.pipeline = pipeline;
    rtStateInfo.sbtBuffer = sbtBuffer;
    rtStateInfo.sbtBufferAllocation = sbtBufferAllocation;

    for (auto& set : rtState.bindingSets()) {
        for (auto& bindingInfo : set->shaderBindings()) {
            for (auto texture : bindingInfo.textures) {
                switch (bindingInfo.type) {
                case ShaderBindingType::TextureSampler:
                case ShaderBindingType::TextureSamplerArray:
                    rtStateInfo.sampledTextures.push_back(texture);
                    break;
                case ShaderBindingType::StorageImage:
                    rtStateInfo.storageImages.push_back(texture);
                    break;
                default:
                    ASSERT_NOT_REACHED();
                }
            }
        }
    }

    size_t index = m_rtStateInfos.add(rtStateInfo);
    rtState.registerBackend(backendBadge(), index);
}

void VulkanBackend::deleteRayTracingState(const RayTracingState& rtState)
{
    if (rtState.id() == Resource::NullId) {
        LogErrorAndExit("Trying to delete an already-deleted or not-yet-created ray tracing state\n");
    }

    RayTracingStateInfo& rtStateInfo = rayTracingStateInfo(rtState);
    vmaFreeMemory(m_memoryAllocator, rtStateInfo.sbtBufferAllocation);
    vkDestroyPipeline(device(), rtStateInfo.pipeline, nullptr);
    vkDestroyPipelineLayout(device(), rtStateInfo.pipelineLayout, nullptr);

    m_rtStateInfos.remove(rtState.id());
    rtState.unregisterBackend(backendBadge());
}

void VulkanBackend::newComputeState(const ComputeState& computeState)
{
    VkPipelineShaderStageCreateInfo computeShaderStage { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    computeShaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeShaderStage.pName = "main";
    {
        const Shader& shader = computeState.shader();
        ASSERT(shader.type() == ShaderType::Compute);
        ASSERT(shader.files().size() == 1);

        const ShaderFile& file = shader.files().front();
        ASSERT(file.type() == ShaderFileType::Compute);

        // TODO: Maybe don't create new modules every time? Currently they are deleted later in this function
        VkShaderModuleCreateInfo moduleCreateInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        const std::vector<uint32_t>& spirv = ShaderManager::instance().spirv(file.path());
        moduleCreateInfo.codeSize = sizeof(uint32_t) * spirv.size();
        moduleCreateInfo.pCode = spirv.data();

        VkShaderModule shaderModule {};
        if (vkCreateShaderModule(device(), &moduleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to create shader module\n");
        }

        computeShaderStage.module = shaderModule;
    }

    //
    // Create pipeline layout
    //

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };

    const auto& [descriptorSetLayouts, pushConstantRange] = createDescriptorSetLayoutForShader(computeState.shader());

    pipelineLayoutCreateInfo.setLayoutCount = descriptorSetLayouts.size();
    pipelineLayoutCreateInfo.pSetLayouts = descriptorSetLayouts.data();

    if (pushConstantRange.has_value()) {
        pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
        pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange.value();
    } else {
        pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
        pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;
    }

    VkPipelineLayout pipelineLayout {};
    if (vkCreatePipelineLayout(device(), &pipelineLayoutCreateInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create pipeline layout\n");
    }

    // (it's *probably* safe to delete these after creating the pipeline layout! no layers are complaining)
    for (const VkDescriptorSetLayout& layout : descriptorSetLayouts) {
        vkDestroyDescriptorSetLayout(device(), layout, nullptr);
    }

    //
    // Create pipeline
    //

    VkComputePipelineCreateInfo pipelineCreateInfo = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };

    pipelineCreateInfo.stage = computeShaderStage;
    pipelineCreateInfo.layout = pipelineLayout;
    pipelineCreateInfo.flags = 0u;

    VkPipeline computePipeline {};
    if (vkCreateComputePipelines(device(), VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &computePipeline) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create compute pipeline\n");
    }

    // Remove shader modules, they are no longer needed after creating the pipeline
    vkDestroyShaderModule(device(), computeShaderStage.module, nullptr);

    ComputeStateInfo computeStateInfo {};
    computeStateInfo.pipelineLayout = pipelineLayout;
    computeStateInfo.pipeline = computePipeline;

    for (auto& set : computeState.bindingSets()) {
        for (auto& bindingInfo : set->shaderBindings()) {
            for (auto texture : bindingInfo.textures) {
                switch (bindingInfo.type) {
                case ShaderBindingType::StorageImage:
                    computeStateInfo.storageImages.push_back(texture);
                    break;
                default:
                    ASSERT_NOT_REACHED();
                }
            }
        }
    }

    size_t index = m_computeStateInfos.add(computeStateInfo);
    computeState.registerBackend(backendBadge(), index);
}

void VulkanBackend::deleteComputeState(const ComputeState& compState)
{
    if (compState.id() == Resource::NullId) {
        LogErrorAndExit("Trying to delete an already-deleted or not-yet-created computestate\n");
    }

    ComputeStateInfo& compStateInfo = computeStateInfo(compState);
    vkDestroyPipeline(device(), compStateInfo.pipeline, nullptr);
    vkDestroyPipelineLayout(device(), compStateInfo.pipelineLayout, nullptr);

    m_computeStateInfos.remove(compState.id());
    compState.unregisterBackend(backendBadge());
}

VulkanBackend::AccelerationStructureInfo& VulkanBackend::accelerationStructureInfo(const BottomLevelAS& blas)
{
    AccelerationStructureInfo& accStructInfo = m_accStructInfos[blas.id()];
    return accStructInfo;
}

VulkanBackend::AccelerationStructureInfo& VulkanBackend::accelerationStructureInfo(const TopLevelAS& tlas)
{
    AccelerationStructureInfo& accStructInfo = m_accStructInfos[tlas.id()];
    return accStructInfo;
}

VulkanBackend::RayTracingStateInfo& VulkanBackend::rayTracingStateInfo(const RayTracingState& rtState)
{
    RayTracingStateInfo& rtStateInfo = m_rtStateInfos[rtState.id()];
    return rtStateInfo;
}

VulkanBackend::ComputeStateInfo& VulkanBackend::computeStateInfo(const ComputeState& compState)
{
    ComputeStateInfo& compStateInfo = m_computeStateInfos[compState.id()];
    return compStateInfo;
}

VulkanBackend::RenderStateInfo& VulkanBackend::renderStateInfo(const RenderState& renderState)
{
    RenderStateInfo& renderStateInfo = m_renderStateInfos[renderState.id()];
    return renderStateInfo;
}

void VulkanBackend::reconstructRenderGraphResources(RenderGraph& renderGraph)
{
    uint32_t numFrameManagers = m_numSwapchainImages;

    // Create new resource managers
    auto nodeRegistry = std::make_unique<Registry>();
    std::vector<std::unique_ptr<Registry>> frameRegistries {};
    for (uint32_t i = 0; i < numFrameManagers; ++i) {
        const RenderTarget& windowRenderTargetForFrame = m_swapchainMockRenderTargets[i];
        frameRegistries.push_back(std::make_unique<Registry>(&windowRenderTargetForFrame));
    }

    // TODO: Fix me, this is stupid..
    std::vector<Registry*> regPointers {};
    regPointers.reserve(frameRegistries.size());
    for (auto& mng : frameRegistries) {
        regPointers.emplace_back(mng.get());
    }

    renderGraph.constructAll(*nodeRegistry, regPointers);

    // First create & replace node resources
    replaceResourcesForRegistry(m_nodeRegistry.get(), nodeRegistry.get());
    m_nodeRegistry = std::move(nodeRegistry);

    // Then create & replace frame resources
    m_frameRegistries.resize(numFrameManagers);
    for (uint32_t i = 0; i < numFrameManagers; ++i) {
        replaceResourcesForRegistry(m_frameRegistries[i].get(), frameRegistries[i].get());
        m_frameRegistries[i] = std::move(frameRegistries[i]);
    }
}

void VulkanBackend::destroyRenderGraphResources()
{
    for (uint32_t swapchainImageIndex = 0; swapchainImageIndex < m_numSwapchainImages; ++swapchainImageIndex) {
        replaceResourcesForRegistry(m_frameRegistries[swapchainImageIndex].get(), nullptr);
    }
    replaceResourcesForRegistry(m_nodeRegistry.get(), nullptr);
}

void VulkanBackend::replaceResourcesForRegistry(Registry* previous, Registry* current)
{
    // TODO: Implement some kind of smart resource diff where we only delete and create resources that actually change.

    // Delete old resources
    if (previous) {
        for (auto& buffer : previous->buffers()) {
            deleteBuffer(buffer);
        }
        for (auto& texture : previous->textures()) {
            deleteTexture(texture);
        }
        for (auto& renderTarget : previous->renderTargets()) {
            deleteRenderTarget(renderTarget);
        }
        for (auto& bindingSet : previous->bindingSets()) {
            deleteBindingSet(bindingSet);
        }
        for (auto& renderState : previous->renderStates()) {
            deleteRenderState(renderState);
        }
        for (auto& bottomLevelAS : previous->bottomLevelAS()) {
            deleteBottomLevelAccelerationStructure(bottomLevelAS);
        }
        for (auto& topLevelAS : previous->topLevelAS()) {
            deleteTopLevelAccelerationStructure(topLevelAS);
        }
        for (auto& rtState : previous->rayTracingStates()) {
            deleteRayTracingState(rtState);
        }
        for (auto& computeState : previous->computeStates()) {
            deleteComputeState(computeState);
        }
    }

    // Create new resources
    if (current) {
        for (auto& buffer : current->buffers()) {
            newBuffer(buffer);
        }
        for (auto& bufferUpdate : current->bufferUpdates()) {
            updateBuffer(bufferUpdate);
        }
        for (auto& texture : current->textures()) {
            newTexture(texture);
        }
        for (auto& textureUpdate : current->textureUpdates()) {
            updateTexture(textureUpdate);
        }
        for (auto& renderTarget : current->renderTargets()) {
            newRenderTarget(renderTarget);
        }
        for (auto& bottomLevelAS : current->bottomLevelAS()) {
            newBottomLevelAccelerationStructure(bottomLevelAS);
        }
        for (auto& topLevelAS : current->topLevelAS()) {
            newTopLevelAccelerationStructure(topLevelAS);
        }
        for (auto& bindingSet : current->bindingSets()) {
            newBindingSet(bindingSet);
        }
        for (auto& renderState : current->renderStates()) {
            newRenderState(renderState);
        }
        for (auto& rtState : current->rayTracingStates()) {
            newRayTracingState(rtState);
        }
        for (auto& computeState : current->computeStates()) {
            newComputeState(computeState);
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
    vkAllocateCommandBuffers(device(), &commandBufferAllocInfo, &oneTimeCommandBuffer);
    AT_SCOPE_EXIT([&] {
        vkFreeCommandBuffers(device(), m_transientCommandPool, 1, &oneTimeCommandBuffer);
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

    if (vkQueueSubmit(m_graphicsQueue.queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        LogError("VulkanBackend::issueSingleTimeCommand(): could not submit the single-time command buffer.\n");
        return false;
    }
    if (vkQueueWaitIdle(m_graphicsQueue.queue) != VK_SUCCESS) {
        LogError("VulkanBackend::issueSingleTimeCommand(): error while waiting for the graphics queue to idle.\n");
        return false;
    }

    return true;
}

bool VulkanBackend::copyBuffer(VkBuffer source, VkBuffer destination, VkDeviceSize size, VkCommandBuffer* commandBuffer) const
{
    VkBufferCopy bufferCopyRegion = {};
    bufferCopyRegion.size = size;
    bufferCopyRegion.srcOffset = 0;
    bufferCopyRegion.dstOffset = 0;

    if (commandBuffer) {
        vkCmdCopyBuffer(*commandBuffer, source, destination, 1, &bufferCopyRegion);
    } else {
        bool success = issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
            vkCmdCopyBuffer(commandBuffer, source, destination, 1, &bufferCopyRegion);
        });
        if (!success) {
            LogError("VulkanBackend::copyBuffer(): error copying buffer, refer to issueSingleTimeCommand errors for more information.\n");
            return false;
        }
    }

    return true;
}

bool VulkanBackend::setBufferMemoryUsingMapping(VmaAllocation allocation, const void* data, VkDeviceSize size)
{
    if (size == 0) {
        return true;
    }

    void* mappedMemory;
    if (vmaMapMemory(m_memoryAllocator, allocation, &mappedMemory) != VK_SUCCESS) {
        LogError("VulkanBackend::setBufferMemoryUsingMapping(): could not map staging buffer.\n");
        return false;
    }
    std::memcpy(mappedMemory, data, size);
    vmaUnmapMemory(m_memoryAllocator, allocation);
    return true;
}

bool VulkanBackend::setBufferDataUsingStagingBuffer(VkBuffer buffer, const void* data, VkDeviceSize size, VkCommandBuffer* commandBuffer)
{
    if (size == 0) {
        return true;
    }

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

    if (!copyBuffer(stagingBuffer, buffer, size, commandBuffer)) {
        LogError("VulkanBackend::setBufferDataUsingStagingBuffer(): could not copy from staging buffer to buffer.\n");
        return false;
    }

    return true;
}

void VulkanBackend::transitionImageLayoutDEBUG(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags imageAspectMask, VkCommandBuffer commandBuffer) const
{
    VkImageMemoryBarrier imageMemoryBarrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };

    imageMemoryBarrier.image = image;
    imageMemoryBarrier.oldLayout = oldLayout;
    imageMemoryBarrier.newLayout = newLayout;

    imageMemoryBarrier.subresourceRange.aspectMask = imageAspectMask;
    imageMemoryBarrier.subresourceRange.baseArrayLayer = 0;
    imageMemoryBarrier.subresourceRange.layerCount = 1;
    imageMemoryBarrier.subresourceRange.baseMipLevel = 0;
    imageMemoryBarrier.subresourceRange.levelCount = 1;

    // Just do the strictest possible barrier so it should at least be valid, albeit slow.
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT;
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT;
    VkPipelineStageFlagBits srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlagBits dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    vkCmdPipelineBarrier(commandBuffer,
                         srcStageMask, dstStageMask,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &imageMemoryBarrier);
}

bool VulkanBackend::transitionImageLayout(VkImage image, bool isDepthFormat, VkImageLayout oldLayout, VkImageLayout newLayout, VkCommandBuffer* currentCommandBuffer) const
{
    if (oldLayout == newLayout) {
        LogWarning("VulkanBackend::transitionImageLayout(): old & new layout identical, ignoring.\n");
        return true;
    }

    VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    imageBarrier.oldLayout = oldLayout;
    imageBarrier.newLayout = newLayout;
    imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    imageBarrier.image = image;
    imageBarrier.subresourceRange.aspectMask = isDepthFormat ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
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

    } else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {

        // Wait for all color attachment writes ...
        sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        // ... before allowing any shaders to read the memory
        destinationStage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {

        // Wait for all memory writes ...
        sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;

        // ... before allowing any shaders to read the memory
        destinationStage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_GENERAL) {

        // Wait for all shader memory reads...
        sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;

        // ... before allowing any memory writes
        destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;

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

bool VulkanBackend::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, bool isDepthImage) const
{
    VkBufferImageCopy region = {};
    region.bufferOffset = 0;

    // (zeros here indicate tightly packed data)
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageOffset = VkOffset3D { 0, 0, 0 };
    region.imageExtent = VkExtent3D { width, height, 1 };

    region.imageSubresource.aspectMask = isDepthImage ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
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

VkBuffer VulkanBackend::createScratchBufferForAccelerationStructure(VkAccelerationStructureNV accelerationStructure, bool updateInPlace, VmaAllocation& allocation) const
{
    if (!m_rtx.has_value()) {
        LogErrorAndExit("Trying to create a RTX scratch buffer, but there is no ray tracing support!\n");
    }

    VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV };
    memoryRequirementsInfo.type = updateInPlace
        ? VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_UPDATE_SCRATCH_NV
        : VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV;

    VkMemoryRequirements2 scratchMemRequirements2;
    memoryRequirementsInfo.accelerationStructure = accelerationStructure;
    m_rtx->vkGetAccelerationStructureMemoryRequirementsNV(device(), &memoryRequirementsInfo, &scratchMemRequirements2);

    VkBufferCreateInfo scratchBufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    scratchBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    scratchBufferCreateInfo.usage = VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;
    scratchBufferCreateInfo.size = scratchMemRequirements2.memoryRequirements.size;

    if (debugMode) {
        // for nsight debugging & similar stuff)
        scratchBufferCreateInfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    VmaAllocationCreateInfo scratchAllocCreateInfo = {};
    scratchAllocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VkBuffer scratchBuffer;
    if (vmaCreateBuffer(m_memoryAllocator, &scratchBufferCreateInfo, &scratchAllocCreateInfo, &scratchBuffer, &allocation, nullptr) != VK_SUCCESS) {
        LogError("VulkanBackend::createScratchBufferForAccelerationStructure(): could not create scratch buffer.\n");
    }

    return scratchBuffer;
}

VkBuffer VulkanBackend::createRTXInstanceBuffer(std::vector<RTGeometryInstance> instances, VmaAllocation& allocation)
{
    if (!m_rtx.has_value()) {
        LogErrorAndExit("Trying to create a RTX instance buffer, but there is no ray tracing support!\n");
    }

    std::vector<VulkanRTX::GeometryInstance> instanceData {};

    for (size_t instanceIdx = 0; instanceIdx < instances.size(); ++instanceIdx) {
        auto& instance = instances[instanceIdx];
        VulkanRTX::GeometryInstance data {};

        data.transform = transpose(instance.transform.worldMatrix());

        const auto& blasInfo = accelerationStructureInfo(instance.blas);
        data.accelerationStructureHandle = blasInfo.handle;

        // TODO: We already have gl_InstanceID for this running index, and this sets gl_InstanceCustomIndexNV.
        //  Here we instead want some other type of data. Probably something that can be passed in.
        data.instanceId = instance.customInstanceId;

        data.mask = 0xff;
        data.instanceOffset = instance.shaderBindingTableOffset;
        data.flags = 0; //VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV;

        instanceData.push_back(data);
    }

    VkDeviceSize totalSize = instanceData.size() * sizeof(VulkanRTX::GeometryInstance);

    VkBufferCreateInfo instanceBufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    instanceBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    instanceBufferCreateInfo.usage = VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;
    instanceBufferCreateInfo.size = totalSize;

    if (debugMode) {
        // for nsight debugging & similar stuff)
        instanceBufferCreateInfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    VmaAllocationCreateInfo instanceAllocCreateInfo = {};
    instanceAllocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    VkBuffer instanceBuffer;
    if (vmaCreateBuffer(m_memoryAllocator, &instanceBufferCreateInfo, &instanceAllocCreateInfo, &instanceBuffer, &allocation, nullptr) != VK_SUCCESS) {
        LogError("VulkanBackend::createRTXInstanceBuffer(): could not create instance buffer.\n");
    }

    if (!setBufferMemoryUsingMapping(allocation, instanceData.data(), totalSize)) {
        LogError("VulkanBackend::createRTXInstanceBuffer(): could not set instance instance buffer data.\n");
    }

    return instanceBuffer;
}

std::pair<std::vector<VkDescriptorSetLayout>, std::optional<VkPushConstantRange>> VulkanBackend::createDescriptorSetLayoutForShader(const Shader& shader) const
{
    uint32_t maxSetId = 0;
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding>> sets;

    std::optional<VkPushConstantRange> pushConstantRange;

    for (auto& file : shader.files()) {

        VkShaderStageFlags stageFlag;
        switch (file.type()) {
        case ShaderFileType::Vertex:
            stageFlag = VK_SHADER_STAGE_VERTEX_BIT;
            break;
        case ShaderFileType::Fragment:
            stageFlag = VK_SHADER_STAGE_FRAGMENT_BIT;
            break;
        case ShaderFileType::Compute:
            stageFlag = VK_SHADER_STAGE_COMPUTE_BIT;
            break;
        case ShaderFileType::RTRaygen:
            stageFlag = VK_SHADER_STAGE_RAYGEN_BIT_NV;
            break;
        case ShaderFileType::RTClosestHit:
            stageFlag = VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;
            break;
        case ShaderFileType::RTAnyHit:
            stageFlag = VK_SHADER_STAGE_ANY_HIT_BIT_NV;
            break;
        case ShaderFileType::RTMiss:
            stageFlag = VK_SHADER_STAGE_MISS_BIT_NV;
            break;
        case ShaderFileType::RTIntersection:
            stageFlag = VK_SHADER_STAGE_INTERSECTION_BIT_NV;
            break;
        default:
            ASSERT_NOT_REACHED();
        }

        const auto& spv = ShaderManager::instance().spirv(file.path());
        spirv_cross::Compiler compiler { spv };
        spirv_cross::ShaderResources resources = compiler.get_shader_resources();

        auto add = [&](const spirv_cross::Resource& res, VkDescriptorType descriptorType) {
            uint32_t setId = compiler.get_decoration(res.id, spv::Decoration::DecorationDescriptorSet);
            auto& set = sets[setId];

            maxSetId = std::max(maxSetId, setId);

            uint32_t bindingId = compiler.get_decoration(res.id, spv::Decoration::DecorationBinding);
            auto entry = set.find(bindingId);
            if (entry == set.end()) {

                uint32_t arrayCount = 1; // i.e. not an array
                const spirv_cross::SPIRType& type = compiler.get_type(res.type_id);
                if (!type.array.empty()) {
                    ASSERT(type.array.size() == 1); // i.e. no multidimensional arrays
                    arrayCount = type.array[0];
                }

                VkDescriptorSetLayoutBinding binding {};
                binding.binding = bindingId;
                binding.stageFlags = stageFlag;
                binding.descriptorCount = arrayCount;
                binding.descriptorType = descriptorType;
                binding.pImmutableSamplers = nullptr;

                set[bindingId] = binding;

            } else {
                set[bindingId].stageFlags |= stageFlag;
            }
        };

        for (auto& ubo : resources.uniform_buffers) {
            add(ubo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        }
        for (auto& sbo : resources.storage_buffers) {
            add(sbo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }
        for (auto& sampledImage : resources.sampled_images) {
            add(sampledImage, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        }
        for (auto& storageImage : resources.storage_images) {
            add(storageImage, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        }
        for (auto& accelerationStructure : resources.acceleration_structures) {
            add(accelerationStructure, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV);
        }

        if (!resources.push_constant_buffers.empty()) {
            ASSERT(resources.push_constant_buffers.size() == 1);
            const spirv_cross::Resource& res = resources.push_constant_buffers[0];
            const spirv_cross::SPIRType& type = compiler.get_type(res.type_id);
            size_t pushConstantSize = compiler.get_declared_struct_size(type);

            if (!pushConstantRange.has_value()) {
                VkPushConstantRange range {};
                range.stageFlags = stageFlag;
                range.size = pushConstantSize;
                range.offset = 0;
                pushConstantRange = range;
            } else {
                if (pushConstantRange.value().size != pushConstantSize) {
                    LogErrorAndExit("Different push constant sizes in the different shader files!\n");
                }
                pushConstantRange.value().stageFlags |= stageFlag;
            }
        }
    }

    std::vector<VkDescriptorSetLayout> setLayouts { maxSetId + 1 };
    for (uint32_t setId = 0; setId <= maxSetId; ++setId) {

        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        std::vector<VkDescriptorSetLayoutBinding> layoutBindings {};

        // There can be no gaps in the list of set layouts when creating a pipeline layout, so we fill them in here
        descriptorSetLayoutCreateInfo.bindingCount = 0;
        descriptorSetLayoutCreateInfo.pBindings = nullptr;

        auto entry = sets.find(setId);
        if (entry != sets.end()) {

            for (auto& [id, binding] : entry->second) {
                layoutBindings.push_back(binding);
            }

            descriptorSetLayoutCreateInfo.bindingCount = layoutBindings.size();
            descriptorSetLayoutCreateInfo.pBindings = layoutBindings.data();
        }

        if (vkCreateDescriptorSetLayout(device(), &descriptorSetLayoutCreateInfo, nullptr, &setLayouts[setId]) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to create descriptor set layout from shader\n");
        }
    }

    return { setLayouts, pushConstantRange };
}

uint32_t VulkanBackend::findAppropriateMemory(uint32_t typeBits, VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice(), &memoryProperties);

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

    if (vkResetFences(device(), 1, inFlight) != VK_SUCCESS) {
        LogError("VulkanBackend::submitQueue(): error resetting in-flight frame fence (index %u).\n", imageIndex);
    }

    VkResult submitStatus = vkQueueSubmit(m_graphicsQueue.queue, 1, &submitInfo, *inFlight);
    if (submitStatus != VK_SUCCESS) {
        LogError("VulkanBackend::submitQueue(): could not submit the graphics queue (index %u).\n", imageIndex);
    }
}
