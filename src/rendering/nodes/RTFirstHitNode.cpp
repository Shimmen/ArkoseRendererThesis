#include "RTFirstHitNode.h"

#include "RTAccelerationStructures.h"
#include "SceneUniformNode.h"
#include "utility/models/SphereSetModel.h"
#include <imgui.h>

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
    std::vector<const Buffer*> vertexBuffers {};
    std::vector<const Buffer*> indexBuffers {};
    std::vector<const Buffer*> sphereBuffers {};

    std::vector<const Texture*> allTextures {};
    std::vector<RTMesh> rtMeshes {};

    auto createTriangleMeshVertexBuffer = [&](const Mesh& mesh) {
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
        Texture* baseColorTexture = material.baseColor.empty()
            ? &nodeReg.createPixelTexture(material.baseColorFactor, true)
            : &nodeReg.loadTexture2D(material.baseColor, true, true);

        size_t texIndex = allTextures.size();
        allTextures.push_back(baseColorTexture);

        rtMeshes.push_back({ .objectId = (int)rtMeshes.size(),
                             .baseColor = (int)texIndex });

        // TODO: Later, we probably want to have combined vertex/ssbo and index/ssbo buffers instead!
        vertexBuffers.push_back(&nodeReg.createBuffer((std::byte*)vertices.data(), vertices.size() * sizeof(RTVertex), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal));
        indexBuffers.push_back(&nodeReg.createBuffer(mesh.indexData(), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal));
    };

    m_scene.forEachModel([&](size_t, const Model& model) {
        model.forEachMesh([&](const Mesh& mesh) {
            createTriangleMeshVertexBuffer(mesh);
        });
        
        if (model.proxy().hasMeshes()) {
            model.proxy().forEachMesh([&](const Mesh& proxyMesh) {
                createTriangleMeshVertexBuffer(proxyMesh);
            });
        } else {
            const auto* sphereSetModel = dynamic_cast<const SphereSetModel*>(&model);
            if (sphereSetModel) {
                std::vector<RTSphere> spheresData;
                for (const auto& sphere : sphereSetModel->spheres()) {

                    vec3 center = vec3(sphere);
                    float radius = sphere.w;
                    vec3 min = center - vec3(radius);
                    vec3 max = center + vec3(radius);

                    RTSphere sphere;
                    sphere.aabbMinX = min.x;
                    sphere.aabbMinY = min.y;
                    sphere.aabbMinZ = min.z;
                    sphere.aabbMaxX = max.x;
                    sphere.aabbMaxY = max.y;
                    sphere.aabbMaxZ = max.z;

                    spheresData.push_back(sphere);
                }
                sphereBuffers.push_back(&nodeReg.createBuffer(std::move(spheresData), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal));
            } else {
                ASSERT_NOT_REACHED();
            }
        }
    });

    Buffer& meshBuffer = nodeReg.createBuffer(std::move(rtMeshes), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal);
    m_objectDataBindingSet = &nodeReg.createBindingSet({ { 0, ShaderStageRTClosestHit, &meshBuffer, ShaderBindingType::StorageBuffer },
                                                         { 1, ShaderStageRTClosestHit, vertexBuffers },
                                                         { 2, ShaderStageRTClosestHit, indexBuffers },
                                                         { 3, ShaderStageRTClosestHit, allTextures, RT_MAX_TEXTURES },
                                                         { 4, ShaderStageRTIntersection, sphereBuffers } });
}

RenderGraphNode::ExecuteCallback RTFirstHitNode::constructFrame(Registry& reg) const
{
    Texture& storageImage = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F, Texture::Usage::StorageAndSample);
    reg.publish("image", storageImage);

    Buffer& timeBuffer = reg.createBuffer(sizeof(float), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    BindingSet& environmentBindingSet = reg.createBindingSet({ { 0, ShaderStageRTMiss, reg.getTexture(SceneUniformNode::name(), "environmentMap") } });

    auto createStateForTLAS = [&](const TopLevelAS& tlas) -> std::pair<BindingSet&, RayTracingState&> {
        BindingSet& frameBindingSet = reg.createBindingSet({ { 0, ShaderStageRTRayGen, &tlas },
                                                             { 1, ShaderStageRTRayGen, &storageImage, ShaderBindingType::StorageImage },
                                                             { 2, ShaderStageRTRayGen, reg.getBuffer(SceneUniformNode::name(), "camera") },
                                                             { 3, ShaderStageRTMiss, &timeBuffer } });

        ShaderFile raygen = ShaderFile("rt-firsthit/raygen.rgen", ShaderFileType::RTRaygen);
        HitGroup mainHitGroup { ShaderFile("rt-firsthit/closestHit.rchit", ShaderFileType::RTClosestHit) };
        HitGroup sphereHitGroup { ShaderFile("rt-firsthit/sphere.rchit", ShaderFileType::RTClosestHit), {}, ShaderFile("rt-firsthit/sphere.rint", ShaderFileType::RTIntersection) };
        ShaderFile missShader { ShaderFile("rt-firsthit/miss.rmiss", ShaderFileType::RTMiss) };
        ShaderBindingTable sbt { raygen, { mainHitGroup, sphereHitGroup }, { missShader } };

        uint32_t maxRecursionDepth = 1;
        RayTracingState& rtState = reg.createRayTracingState(sbt, { &frameBindingSet, m_objectDataBindingSet, &environmentBindingSet }, maxRecursionDepth);

        return { frameBindingSet, rtState };
    };

    const TopLevelAS& mainTLAS = *reg.getTopLevelAccelerationStructure(RTAccelerationStructures::name(), "scene");
    auto& [frameBindingSet, rtState] = createStateForTLAS(mainTLAS);

    const TopLevelAS& proxyTLAS = *reg.getTopLevelAccelerationStructure(RTAccelerationStructures::name(), "proxy");
    auto& [frameBindingSetProxy, rtStateProxy] = createStateForTLAS(proxyTLAS);

    return [&](const AppState& appState, CommandList& cmdList) {
        static bool useProxies = true;
        if (ImGui::CollapsingHeader("RT first-hit")) {
            ImGui::Checkbox("Use proxies", &useProxies);
        }

        if (useProxies) {
            cmdList.setRayTracingState(rtStateProxy);
            cmdList.bindSet(frameBindingSetProxy, 0);
        } else {
            cmdList.setRayTracingState(rtState);
            cmdList.bindSet(frameBindingSet, 0);
        }

        float time = appState.elapsedTime();
        cmdList.updateBufferImmediately(timeBuffer, &time, sizeof(time));

        cmdList.bindSet(*m_objectDataBindingSet, 1);
        cmdList.bindSet(environmentBindingSet, 2);
        cmdList.traceRays(appState.windowExtent());
    };
}
