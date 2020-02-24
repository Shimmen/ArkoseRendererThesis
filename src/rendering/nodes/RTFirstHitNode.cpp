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
    m_materials.clear();

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
                    vertices.push_back({ .position = posData[i],
                                         .normal = normalData[i],
                                         .texCoord = texCoordData[i] });
                }
            }

            const Material& material = mesh.material();
            Texture* baseColorTexture { nullptr };
            if (material.baseColor.empty()) {
                baseColorTexture = &nodeReg.createPixelTexture(material.baseColorFactor, true);
            } else {
                baseColorTexture = &nodeReg.loadTexture2D(material.baseColor, true, true);
            }

            size_t texIndex = m_textures.size();
            m_textures.push_back(baseColorTexture);

            RTMaterial rtMaterial { .baseColor = (int)texIndex };
            m_materials.push_back(rtMaterial);

            // TODO: We want to specify if the geometry is opaque or not also!
            RTGeometry geometry { .vertexBuffer = nodeReg.createBuffer(std::move(vertices), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal),
                                  .vertexFormat = VertexFormat::XYZ32F,
                                  .vertexStride = sizeof(RTVertex),
                                  .indexBuffer = nodeReg.createBuffer(mesh.indexData(), Buffer::Usage::Index, Buffer::MemoryHint::GpuOptimal),
                                  .indexType = mesh.indexType(),
                                  .transform = mesh.transform().localMatrix() };

            BottomLevelAS& blas = nodeReg.createBottomLevelAccelerationStructure({ geometry });
            m_instances.push_back({ .blas = blas,
                                    .transform = model->transform() });
        });
    }
}

RenderGraphNode::ExecuteCallback RTFirstHitNode::constructFrame(Registry& reg) const
{
    Texture& storageImage = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F, Texture::Usage::StorageAndSample);
    reg.publish("image", storageImage);

    ShaderFile raygen = ShaderFile("rt-firsthit/raygen.rgen", ShaderFileType::RTRaygen);
    ShaderFile miss = ShaderFile("rt-firsthit/miss.rmiss", ShaderFileType::RTMiss);
    ShaderFile closestHit = ShaderFile("rt-firsthit/closestHit.rchit", ShaderFileType::RTClosestHit);

    TopLevelAS& tlas = reg.createTopLevelAccelerationStructure(m_instances);
    BindingSet& bindingSet = reg.createBindingSet({ { 0, ShaderStageRTRayGen, &tlas },
                                                    { 1, ShaderStageRTRayGen, &storageImage, ShaderBindingType::StorageImage },
                                                    { 2, ShaderStageRTRayGen, reg.getBuffer(CameraUniformNode::name(), "buffer") } });

    RayTracingState& rtState = reg.createRayTracingState({ raygen, miss, closestHit }, { &bindingSet });

    return [&](const AppState& appState, CommandList& cmdList) {
        cmdList.rebuildTopLevelAcceratationStructure(tlas);
        cmdList.setRayTracingState(rtState);
        cmdList.bindSet(bindingSet, 0);
        cmdList.traceRays(appState.windowExtent());
    };
}
