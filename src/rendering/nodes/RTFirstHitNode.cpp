#include "RTFirstHitNode.h"

#include "CameraUniformNode.h"

RTFirstHitNode::RTFirstHitNode(const Scene& scene)
    : RenderGraphNode(RTFirstHitNode::name())
    , m_scene(scene)
{
}

std::string RTFirstHitNode::name()
{
    return "rt-firsthit";
}

void RTFirstHitNode::constructNode(Registry& nodeReg)
{
    m_instances.clear();

    std::vector<const Buffer*> vertexBuffers {};
    std::vector<const Buffer*> indexBuffers {};
    std::vector<RTMesh> rtMeshes {};
    std::vector<const Texture*> allTextures {};

    for (auto& model : m_scene.models()) {
        model->forEachMesh([&](const Mesh& mesh) {
            std::vector<RTVertex> vertices {};
            {
                auto posData = mesh.positionData();
                auto normalData = mesh.normalData();
                auto texCoordData = mesh.texcoordData();

                ASSERT(posData.size() == normalData.size());
                ASSERT(posData.size() == texCoordData.size());

                for (int i = 0; i < posData.size(); ++i) {
                    vertices.push_back({ .position = vec4(posData[i], 0.0f),
                                         .normal = vec4(normalData[i], 0.0f),
                                         .texCoord = vec4(texCoordData[i], 0.0f, 0.0f) });
                }
            }

            const Material& material = mesh.material();
            Texture* baseColorTexture { nullptr };
            if (material.baseColor.empty()) {
                baseColorTexture = &nodeReg.createPixelTexture(material.baseColorFactor, true);
            } else {
                baseColorTexture = &nodeReg.loadTexture2D(material.baseColor, true, true);
            }

            size_t texIndex = allTextures.size();
            allTextures.push_back(baseColorTexture);

            rtMeshes.push_back({ .objectId = (int)m_instances.size(),
                                 .baseColor = (int)texIndex });

            // TODO: Later, we probably want to have combined vertex/ssbo and index/ssbo buffers instead!
            vertexBuffers.push_back(&nodeReg.createBuffer((std::byte*)vertices.data(), vertices.size() * sizeof(RTVertex), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal));
            indexBuffers.push_back(&nodeReg.createBuffer(mesh.indexData(), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal));

            RTGeometry geometry { .vertexBuffer = nodeReg.createBuffer(std::move(vertices), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal),
                                  .vertexFormat = VertexFormat::XYZ32F,
                                  .vertexStride = sizeof(RTVertex),
                                  .indexBuffer = nodeReg.createBuffer(mesh.indexData(), Buffer::Usage::Index, Buffer::MemoryHint::GpuOptimal),
                                  .indexType = mesh.indexType(),
                                  .transform = mesh.transform().localMatrix() };

            // TODO: Later we probably want to keep all meshes of a model in a single BLAS, but that requires some fancy SBT stuff which I don't wanna mess with now.
            BottomLevelAS& blas = nodeReg.createBottomLevelAccelerationStructure({ geometry });
            m_instances.push_back({ .blas = blas,
                                    .transform = model->transform() });
        });
    }

    Texture& environmentTexture = nodeReg.loadTexture2D(m_scene.environmentMap(), true, false);
    m_environmentBindingSet = &nodeReg.createBindingSet({ { 0, ShaderStageRTMiss, &environmentTexture } });

    Buffer& meshBuffer = nodeReg.createBuffer(std::move(rtMeshes), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal);
    m_objectDataBindingSet = &nodeReg.createBindingSet({ { 0, ShaderStageRTClosestHit, &meshBuffer, ShaderBindingType::StorageBuffer },
                                                         { 1, ShaderStageRTClosestHit, vertexBuffers },
                                                         { 2, ShaderStageRTClosestHit, indexBuffers },
                                                         { 3, ShaderStageRTClosestHit, allTextures, RT_MAX_TEXTURES } });
}

RenderGraphNode::ExecuteCallback RTFirstHitNode::constructFrame(Registry& reg) const
{
    Texture& storageImage = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F, Texture::Usage::StorageAndSample);
    reg.publish("image", storageImage);

    ShaderFile raygen = ShaderFile("rt-firsthit/raygen.rgen", ShaderFileType::RTRaygen);
    ShaderFile miss = ShaderFile("rt-firsthit/miss.rmiss", ShaderFileType::RTMiss);
    ShaderFile closestHit = ShaderFile("rt-firsthit/closestHit.rchit", ShaderFileType::RTClosestHit);

    Buffer& timeBuffer = reg.createBuffer(sizeof(float), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);

    TopLevelAS& tlas = reg.createTopLevelAccelerationStructure(m_instances);
    BindingSet& frameBindingSet = reg.createBindingSet({ { 0, ShaderStageRTRayGen, &tlas },
                                                         { 1, ShaderStageRTRayGen, &storageImage, ShaderBindingType::StorageImage },
                                                         { 2, ShaderStageRTRayGen, reg.getBuffer(CameraUniformNode::name(), "buffer") },
                                                         { 3, ShaderStageRTMiss, &timeBuffer } });

    uint32_t maxRecursionDepth = 1;
    RayTracingState& rtState = reg.createRayTracingState({ raygen, miss, closestHit }, { &frameBindingSet, m_objectDataBindingSet, m_environmentBindingSet }, maxRecursionDepth);

    return [&](const AppState& appState, CommandList& cmdList) {
        cmdList.rebuildTopLevelAcceratationStructure(tlas);
        cmdList.setRayTracingState(rtState);

        float time = appState.elapsedTime();
        cmdList.updateBufferImmediately(timeBuffer, &time, sizeof(time));
        cmdList.bindSet(frameBindingSet, 0);

        cmdList.bindSet(*m_objectDataBindingSet, 1);
        cmdList.bindSet(*m_environmentBindingSet, 2);
        cmdList.traceRays(appState.windowExtent());
    };
}
