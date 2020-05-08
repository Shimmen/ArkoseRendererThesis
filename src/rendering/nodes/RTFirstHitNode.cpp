#include "RTFirstHitNode.h"

#include "RTAccelerationStructures.h"
#include "SceneUniformNode.h"
#include "utility/models/SphereSetModel.h"
#include "utility/models/VoxelContourModel.h"
#include <half.hpp>
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
    std::vector<const Buffer*> shBuffers {};

    std::vector<const Buffer*> contourPlaneBuffers {};
    std::vector<const Buffer*> contourAabbBuffers {};

    std::vector<vec4> contourColors {};
    std::vector<const Buffer*> contourColorIdxBuffers {};

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
                                     .normal = vec4(mesh.transform().localNormalMatrix() * normalData[i], 0.0f),
                                     .texCoord = vec4(texCoordData[i], 0.0f, 0.0f) });
            }
        }

        const Material& material = mesh.material();
        Texture* baseColorTexture = material.baseColor.empty()
            ? &nodeReg.createPixelTexture(material.baseColorFactor, false) // the color is already in linear sRGB so we don't want to make an sRGB texture for it!
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
            const auto* sphereSetModel = dynamic_cast<const SphereSetModel*>(&model.proxy());
            if (sphereSetModel) {
                using namespace half_float;
                std::vector<half> spheresData;
                for (const auto& sphere : sphereSetModel->spheres()) {
                    spheresData.push_back(half(sphere.x));
                    spheresData.push_back(half(sphere.y));
                    spheresData.push_back(half(sphere.z));
                    spheresData.push_back(half(sphere.w));
                }
                sphereBuffers.push_back(&nodeReg.createBuffer(std::move(spheresData), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal));

                auto shData = sphereSetModel->sphericalHarmonics();
                shBuffers.push_back(&nodeReg.createBuffer(std::move(shData), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal));

                return;
            }

            const auto* voxelContourModel = dynamic_cast<const VoxelContourModel*>(&model.proxy());
            if (voxelContourModel) {
                using namespace half_float;
                std::vector<half> contourPlaneData;
                std::vector<half> contourAabbData;
                std::vector<uint32_t> contourColorIdxData;
                for (const auto& contour : voxelContourModel->contours()) {

                    contourPlaneData.push_back(half(contour.normal.x));
                    contourPlaneData.push_back(half(contour.normal.y));
                    contourPlaneData.push_back(half(contour.normal.z));
                    contourPlaneData.push_back(half(contour.distance));

                    contourAabbData.push_back(half(contour.aabb.min.x));
                    contourAabbData.push_back(half(contour.aabb.min.y));
                    contourAabbData.push_back(half(contour.aabb.min.z));
                    contourAabbData.push_back(half(contour.aabb.max.x));
                    contourAabbData.push_back(half(contour.aabb.max.y));
                    contourAabbData.push_back(half(contour.aabb.max.z));

                    size_t colorIdxOffset = contourColors.size();
                    size_t index = colorIdxOffset + contour.colorIndex;
                    ASSERT(index < UINT32_MAX);
                    contourColorIdxData.push_back(static_cast<uint32_t>(index));
                }

                for (const vec3& color : voxelContourModel->colors()) {
                    contourColors.push_back(vec4(color, 0.0));
                }

                contourPlaneBuffers.push_back(&nodeReg.createBuffer(std::move(contourPlaneData), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal));
                contourAabbBuffers.push_back(&nodeReg.createBuffer(std::move(contourAabbData), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal));
                contourColorIdxBuffers.push_back(&nodeReg.createBuffer(std::move(contourColorIdxData), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal));

                return;
            }

            ASSERT_NOT_REACHED();
        }
    });

    Buffer& meshBuffer = nodeReg.createBuffer(std::move(rtMeshes), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal);
    Buffer& contourColorBuffer = nodeReg.createBuffer(std::move(contourColors), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal);
    m_objectDataBindingSet = &nodeReg.createBindingSet({ { 0, ShaderStageRTClosestHit, &meshBuffer, ShaderBindingType::StorageBuffer },
                                                         { 1, ShaderStageRTClosestHit, vertexBuffers },
                                                         { 2, ShaderStageRTClosestHit, indexBuffers },
                                                         { 3, ShaderStageRTClosestHit, allTextures, RT_MAX_TEXTURES },
                                                         { 4, ShaderStageRTIntersection, sphereBuffers },
                                                         { 5, ShaderStageRTClosestHit, shBuffers },
                                                         { 6, ShaderStageRTIntersection, contourPlaneBuffers },
                                                         { 7, ShaderStageRTIntersection, contourAabbBuffers },
                                                         { 8, ShaderStageRTIntersection, contourColorIdxBuffers },
                                                         { 9, ShaderStageRTClosestHit, &contourColorBuffer, ShaderBindingType::StorageBuffer } });
}

RenderGraphNode::ExecuteCallback RTFirstHitNode::constructFrame(Registry& reg) const
{
    Texture& storageImage = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F, Texture::Usage::StorageAndSample);
    reg.publish("image", storageImage);

    Buffer& timeBuffer = reg.createBuffer(sizeof(float), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    BindingSet& environmentBindingSet = reg.createBindingSet({ { 0, ShaderStageRTMiss, reg.getTexture(SceneUniformNode::name(), "environmentMap").value_or(&reg.createPixelTexture(vec4(1), true)) } });

    auto createStateForTLAS = [&](const TopLevelAS& tlas) -> std::pair<BindingSet&, RayTracingState&> {
        BindingSet& frameBindingSet = reg.createBindingSet({ { 0, ShaderStageRTRayGen, &tlas },
                                                             { 1, ShaderStageRTRayGen, &storageImage, ShaderBindingType::StorageImage },
                                                             { 2, ShaderStageRTRayGen, reg.getBuffer(SceneUniformNode::name(), "camera") },
                                                             { 3, ShaderStageRTMiss, &timeBuffer } });

        ShaderFile raygen = ShaderFile("rt-firsthit/raygen.rgen");
        HitGroup mainHitGroup { ShaderFile("rt-firsthit/closestHit.rchit") };
        HitGroup sphereHitGroup { ShaderFile("rt-firsthit/sphere.rchit"), {}, ShaderFile("rt-firsthit/sphere.rint") };
        HitGroup contourHitGroup { ShaderFile("rt-firsthit/contour.rchit"), {}, ShaderFile("rt-firsthit/contour.rint") };
        ShaderFile missShader { ShaderFile("rt-firsthit/miss.rmiss") };
        ShaderBindingTable sbt { raygen, { mainHitGroup, sphereHitGroup, contourHitGroup }, { missShader } };

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
        ImGui::Checkbox("Use proxies", &useProxies);

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
