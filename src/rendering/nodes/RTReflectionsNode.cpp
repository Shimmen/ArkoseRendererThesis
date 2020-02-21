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
    std::vector<RTGeometryInstance> instances {};

    for (const auto model : m_scene.models()) {

        std::vector<RTGeometry> geometries {};
        model->forEachMesh([&](const Mesh& mesh) {
            // TODO: Somehow include the mesh.transform().localMatrix() in the geometry data!
            // TODO: We want to specify if the geometry is opaque or not also!
            geometries.push_back({ .vertexBuffer = nodeReg.createBuffer(mesh.positionData(), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal),
                                   .vertexFormat = VertexFormat::XYZ32F,
                                   .indexBuffer = nodeReg.createBuffer(mesh.indexData(), Buffer::Usage::Index, Buffer::MemoryHint::GpuOptimal),
                                   .indexType = IndexType::UInt16 });
        });

        BottomLevelAS& blas = nodeReg.createBottomLevelAccelerationStructure(geometries);
        instances.push_back({ .blas = blas,
                              .transform = model->transform().worldMatrix() });
    }

    m_tlas = &nodeReg.createTopLevelAccelerationStructure(instances);
}

RenderGraphNode::ExecuteCallback RTReflectionsNode::constructFrame(Registry& reg) const
{
    Texture& storageImage = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F, Texture::Usage::StorageAndSample);
    reg.publish("image", storageImage);

    ShaderFile raygen = ShaderFile("rt-reflections/raygen.rgen", ShaderFileType::RTRaygen);
    ShaderFile miss = ShaderFile("rt-reflections/miss.rmiss", ShaderFileType::RTMiss);
    ShaderFile closestHit = ShaderFile("rt-reflections/closestHit.rchit", ShaderFileType::RTClosestHit);

    // Maybe we want frame-reg TLAS? Since we want to update & compact it, etc.
    BindingSet& bindingSet = reg.createBindingSet({ { 0, ShaderStageRTRayGen, m_tlas },
                                                    { 1, ShaderStageRTRayGen, &storageImage },
                                                    { 2, ShaderStageRTRayGen, reg.getBuffer(CameraUniformNode::name(), "buffer") } });

    RayTracingState& rtState = reg.createRayTracingState({ raygen, miss, closestHit }, { &bindingSet });

    return [&](const AppState& appState, CommandList& cmdList) {
        cmdList.setRayTracingState(rtState);
        cmdList.bindSet(bindingSet, 0);
        cmdList.traceRays(appState.windowExtent());
    };
}
