#pragma once

#include "VulkanCore.h"
#include "VulkanRTX.h"
#include "rendering/App.h"
#include "rendering/Backend.h"
#include "utility/PersistentIndexedList.h"
#include <array>
#include <optional>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

struct GLFWwindow;

constexpr bool vulkanDebugMode = true;

class VulkanBackend final : public Backend {
public:
    VulkanBackend(GLFWwindow*, App&);
    ~VulkanBackend() final;

    VulkanBackend(VulkanBackend&&) = delete;
    VulkanBackend(VulkanBackend&) = delete;
    VulkanBackend& operator=(VulkanBackend&) = delete;

    // There might be more elegant ways of giving access. We really don't need everything from here.
    friend class VulkanCommandList;

    ///////////////////////////////////////////////////////////////////////////
    /// Public backend API

    bool executeFrame(double elapsedTime, double deltaTime, bool renderGui) override;

private:
    ///////////////////////////////////////////////////////////////////////////
    /// Utilities

    VkDevice device() const
    {
        return m_core->device();
    }

    VkPhysicalDevice physicalDevice() const
    {
        return m_core->physicalDevice();
    }

    ///////////////////////////////////////////////////////////////////////////
    /// Command translation & resource management

    void reconstructRenderGraphResources(RenderGraph& renderGraph);
    void destroyRenderGraphResources(); // TODO: This is a weird function now..

    void replaceResourcesForRegistry(Registry* previous, Registry* current);

    void newBuffer(const Buffer&);
    void deleteBuffer(const Buffer&);
    void updateBuffer(const BufferUpdate&);
    void updateBuffer(const Buffer& buffer, const std::byte*, size_t);

    void newTexture(const Texture&);
    void deleteTexture(const Texture&);
    void updateTexture(const TextureUpdate&);
    void generateMipmaps(const Texture&, VkImageLayout finalLayout);

    void newRenderTarget(const RenderTarget&);
    void deleteRenderTarget(const RenderTarget&);
    void setupWindowRenderTargets();
    void destroyWindowRenderTargets();

    void newBindingSet(const BindingSet&);
    void deleteBindingSet(const BindingSet&);

    void newRenderState(const RenderState&);
    void deleteRenderState(const RenderState&);

    // Maybe move to VulkanRTX or similar. In theory we might want multiple possible RT backends, e.g. OptiX vs RTX
    void newBottomLevelAccelerationStructure(const BottomLevelAS&);
    void deleteBottomLevelAccelerationStructure(const BottomLevelAS&);

    void newTopLevelAccelerationStructure(const TopLevelAS&);
    void deleteTopLevelAccelerationStructure(const TopLevelAS&);

    void newRayTracingState(const RayTracingState&);
    void deleteRayTracingState(const RayTracingState&);

    void newComputeState(const ComputeState&);
    void deleteComputeState(const ComputeState&);

    ///////////////////////////////////////////////////////////////////////////
    /// Drawing

    void drawFrame(const AppState&, double elapsedTime, double deltaTime, bool renderGui, uint32_t swapchainImageIndex);

    ///////////////////////////////////////////////////////////////////////////
    /// Swapchain management

    void submitQueue(uint32_t imageIndex, VkSemaphore* waitFor, VkSemaphore* signal, VkFence* inFlight);

    void createSemaphoresAndFences(VkDevice);

    void createAndSetupSwapchain(VkPhysicalDevice, VkDevice, VkSurfaceKHR);
    void destroySwapchain();
    Extent2D recreateSwapchain();

    void createWindowRenderTargetFrontend();

    ///////////////////////////////////////////////////////////////////////////
    /// ImGui related

    void setupDearImgui();
    void destroyDearImgui();

    void updateDearImguiFramebuffers();
    void renderDearImguiFrame(VkCommandBuffer, uint32_t swapchainImageIndex);

    bool m_guiIsSetup { false };
    VkDescriptorPool m_guiDescriptorPool {};
    VkRenderPass m_guiRenderPass {};
    std::vector<VkFramebuffer> m_guiFramebuffers {};

    ///////////////////////////////////////////////////////////////////////////
    /// Internal and low level Vulkan resource API. Maybe to be removed at some later time.

    bool issueSingleTimeCommand(const std::function<void(VkCommandBuffer)>& callback) const;

    bool copyBuffer(VkBuffer source, VkBuffer destination, VkDeviceSize size, VkCommandBuffer* = nullptr) const;
    bool setBufferMemoryUsingMapping(VmaAllocation, const void* data, VkDeviceSize size);
    bool setBufferDataUsingStagingBuffer(VkBuffer, const void* data, VkDeviceSize size, VkCommandBuffer* = nullptr);

    void transitionImageLayoutDEBUG(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags, VkCommandBuffer) const;

    bool transitionImageLayout(VkImage, bool isDepthFormat, VkImageLayout oldLayout, VkImageLayout newLayout, VkCommandBuffer* = nullptr) const;
    bool copyBufferToImage(VkBuffer, VkImage, uint32_t width, uint32_t height, bool isDepthImage) const;

    VkBuffer createScratchBufferForAccelerationStructure(VkAccelerationStructureNV, bool updateInPlace, VmaAllocation&) const;
    VkBuffer createRTXInstanceBuffer(std::vector<RTGeometryInstance>, VmaAllocation&);

    std::pair<std::vector<VkDescriptorSetLayout>, std::optional<VkPushConstantRange>> createDescriptorSetLayoutForShader(const Shader&) const;

    uint32_t findAppropriateMemory(uint32_t typeBits, VkMemoryPropertyFlags properties) const;

    ///////////////////////////////////////////////////////////////////////////
    /// Window and swapchain related members

    GLFWwindow* m_window;

    VkSwapchainKHR m_swapchain {};
    VulkanQueue m_presentQueue {};

    Extent2D m_swapchainExtent {};
    uint32_t m_numSwapchainImages {};

    VkFormat m_swapchainImageFormat {};
    std::vector<VkImage> m_swapchainImages {};
    std::vector<VkImageView> m_swapchainImageViews {};

    Texture m_swapchainDepthTexture {};

    std::vector<VkFramebuffer> m_swapchainFramebuffers {};
    VkRenderPass m_swapchainRenderPass {};

    //

    static constexpr size_t maxFramesInFlight { 2 };
    uint32_t m_currentFrameIndex { 0 };

    std::array<VkSemaphore, maxFramesInFlight> m_imageAvailableSemaphores {};
    std::array<VkSemaphore, maxFramesInFlight> m_renderFinishedSemaphores {};
    std::array<VkFence, maxFramesInFlight> m_inFlightFrameFences {};

    ///////////////////////////////////////////////////////////////////////////
    /// Sub-systems

    // TODO: Add swapchain management sub-system?
    std::unique_ptr<VulkanCore> m_core {};
    std::optional<VulkanRTX> m_rtx {};

    ///////////////////////////////////////////////////////////////////////////
    /// Resource & resource management members

    VmaAllocator m_memoryAllocator {};

    App& m_app;

    std::unique_ptr<Registry> m_nodeRegistry {};
    std::vector<std::unique_ptr<Registry>> m_frameRegistries {};

    VulkanQueue m_graphicsQueue {};

    std::vector<VkEvent> m_events {};

    VkCommandPool m_renderGraphFrameCommandPool {};
    VkCommandPool m_transientCommandPool {};

    std::vector<VkCommandBuffer> m_frameCommandBuffers {};

    std::unique_ptr<RenderGraph> m_renderGraph {};

    struct BufferInfo {
        VkBuffer buffer {};
        VmaAllocation allocation {};
    };

    struct TextureInfo {
        VkImage image {};
        VmaAllocation allocation {};

        VkFormat format {};
        VkImageView view {};
        VkSampler sampler {};

        VkImageLayout currentLayout {};
    };

    struct RenderTargetInfo {
        VkFramebuffer framebuffer {};
        VkRenderPass compatibleRenderPass {};

        std::vector<std::pair<const Texture*, VkImageLayout>> attachedTextures {};
    };

    struct BindingSetInfo {
        VkDescriptorPool descriptorPool {};
        VkDescriptorSetLayout descriptorSetLayout {};
        VkDescriptorSet descriptorSet {};
    };

    struct RenderStateInfo {
        VkPipelineLayout pipelineLayout {};
        VkPipeline pipeline {};

        std::vector<const Texture*> sampledTextures {};
    };

    struct AccelerationStructureInfo {
        VkAccelerationStructureNV accelerationStructure {};
        VkDeviceMemory memory {};
        uint64_t handle {};

        std::vector<std::pair<VkBuffer, VmaAllocation>> associatedBuffers {};
    };

    struct RayTracingStateInfo {
        VkPipelineLayout pipelineLayout {};
        VkPipeline pipeline {};

        VkBuffer sbtBuffer {};
        VmaAllocation sbtBufferAllocation {};

        std::vector<const Texture*> sampledTextures {};
        std::vector<const Texture*> storageImages {};
    };

    struct ComputeStateInfo {
        VkPipelineLayout pipelineLayout {};
        VkPipeline pipeline {};

        std::vector<const Texture*> storageImages {};
    };

    // (helpers for accessing from *Infos vectors)
    BufferInfo& bufferInfo(const Buffer&);
    TextureInfo& textureInfo(const Texture&);
    RenderTargetInfo& renderTargetInfo(const RenderTarget&);
    BindingSetInfo& bindingSetInfo(const BindingSet&);
    RenderStateInfo& renderStateInfo(const RenderState&);
    AccelerationStructureInfo& accelerationStructureInfo(const BottomLevelAS&);
    AccelerationStructureInfo& accelerationStructureInfo(const TopLevelAS&);
    RayTracingStateInfo& rayTracingStateInfo(const RayTracingState&);
    ComputeStateInfo& computeStateInfo(const ComputeState&);

    PersistentIndexedList<BufferInfo> m_bufferInfos {};
    PersistentIndexedList<TextureInfo> m_textureInfos {};
    PersistentIndexedList<RenderTargetInfo> m_renderTargetInfos {};
    PersistentIndexedList<BindingSetInfo> m_bindingSetInfos {};
    PersistentIndexedList<RenderStateInfo> m_renderStateInfos {};
    PersistentIndexedList<AccelerationStructureInfo> m_accStructInfos {};
    PersistentIndexedList<RayTracingStateInfo> m_rtStateInfos {};
    PersistentIndexedList<ComputeStateInfo> m_computeStateInfos {};

    std::vector<Texture> m_swapchainMockColorTextures {};
    std::vector<RenderTarget> m_swapchainMockRenderTargets {};
};
