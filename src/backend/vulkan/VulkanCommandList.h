#include "../CommandList.h"

#include "VulkanBackend.h"

class VulkanCommandList : public CommandList {
public:
    explicit VulkanCommandList(VulkanBackend&, VkCommandBuffer);

    void updateBufferImmediately(Buffer& buffer, void* pVoid, size_t size) override;

    void setRenderState(const RenderState&, ClearColor, float clearDepth, uint32_t clearStencil) override;
    void bindSet(BindingSet&, uint32_t index) override;
    void draw(Buffer& vertexBuffer, uint32_t vertexCount) override;
    void drawIndexed(Buffer& vertexBuffer, Buffer& indexBuffer, uint32_t indexCount, uint32_t instanceIndex) override;
    void debugBarrier() override;

    void endNode(Badge<VulkanBackend>);

private:
    void endCurrentRenderPassIfAny();

private:
    VulkanBackend& m_backend;
    VkCommandBuffer m_commandBuffer;

    const RenderState* activeRenderState = nullptr;
};
