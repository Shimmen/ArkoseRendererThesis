#include "RTReflectionsNode.h"

#include "CameraUniformNode.h"

RTReflectionsNode::RTReflectionsNode(const Scene& scene)
    : RenderGraphNode(RTReflectionsNode::name())
    , m_scene(scene)
{
}

std::string RTReflectionsNode::name()
{
    return "rt-reflections";
}

void RTReflectionsNode::constructNode(Registry& nodeReg)
{
    m_instances.clear();

    for (auto& model : m_scene.models()) {

        std::vector<RTGeometry> geometries {};
        model->forEachMesh([&](const Mesh& mesh) {
            // TODO: We want to specify if the geometry is opaque or not also!
            geometries.push_back({ .vertexBuffer = nodeReg.createBuffer(mesh.positionData(), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal),
                                   .vertexFormat = mesh.vertexFormat(),
                                   .indexBuffer = nodeReg.createBuffer(mesh.indexData(), Buffer::Usage::Index, Buffer::MemoryHint::GpuOptimal),
                                   .indexType = mesh.indexType(),
                                   .transform = mesh.transform().localMatrix() });
        });

        BottomLevelAS& blas = nodeReg.createBottomLevelAccelerationStructure(geometries);
        m_instances.push_back({ .blas = blas,
                                .transform = model->transform() });
    }
}

RenderGraphNode::ExecuteCallback RTReflectionsNode::constructFrame(Registry& reg) const
{
    Texture& storageImage = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F, Texture::Usage::StorageAndSample);
    reg.publish("image", storageImage);

    ShaderFile raygen = ShaderFile("rt-reflections/raygen.rgen", ShaderFileType::RTRaygen);
    ShaderFile miss = ShaderFile("rt-reflections/miss.rmiss", ShaderFileType::RTMiss);
    ShaderFile closestHit = ShaderFile("rt-reflections/closestHit.rchit", ShaderFileType::RTClosestHit);

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
