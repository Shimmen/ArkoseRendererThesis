#include "RTDiffuseGINode.h"

#include "ForwardRenderNode.h"
#include "LightData.h"
#include "RTAccelerationStructures.h"
#include "SceneUniformNode.h"
#include "utility/GlobalState.h"
#include "utility/models/SphereSetModel.h"
#include <imgui.h>

RTDiffuseGINode::RTDiffuseGINode(const Scene& scene)
    : RenderGraphNode(RTDiffuseGINode::name())
    , m_scene(scene)
{
}

std::string RTDiffuseGINode::name()
{
    return "rt-diffuse-gi";
}

void RTDiffuseGINode::constructNode(Registry& nodeReg)
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
        Texture* baseColorTexture { nullptr };
        if (material.baseColor.empty()) {
            baseColorTexture = &nodeReg.createPixelTexture(material.baseColorFactor, true);
        } else {
            baseColorTexture = &nodeReg.loadTexture2D(material.baseColor, true, true);
        }

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
                std::vector<RTSphere> spheresData;
                for (const auto& sphere : sphereSetModel->spheres()) {

                    RTSphere rtSphere;
                    rtSphere.center = vec3(sphere);
                    rtSphere.radius = sphere.w;

                    spheresData.push_back(rtSphere);
                }
                sphereBuffers.push_back(&nodeReg.createBuffer(std::move(spheresData), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal));
            } else {
                ASSERT_NOT_REACHED();
            }
            LogInfo("Ignoring sphere sets in RTDiffuseGINode\n");
        }
    });

    Buffer& meshBuffer = nodeReg.createBuffer(std::move(rtMeshes), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal);
    m_objectDataBindingSet = &nodeReg.createBindingSet({ { 0, ShaderStageRTClosestHit, &meshBuffer, ShaderBindingType::StorageBuffer },
                                                         { 1, ShaderStageRTClosestHit, vertexBuffers },
                                                         { 2, ShaderStageRTClosestHit, indexBuffers },
                                                         { 3, ShaderStageRTClosestHit, allTextures, RT_MAX_TEXTURES },
                                                         { 4, ShaderStageRTIntersection, sphereBuffers } });

    Extent2D windowExtent = GlobalState::get().windowExtent();
    m_accumulationTexture = &nodeReg.createTexture2D(windowExtent, Texture::Format::RGBA16F, Texture::Usage::StorageAndSample);
}

RenderGraphNode::ExecuteCallback RTDiffuseGINode::constructFrame(Registry& reg) const
{
    const Texture* gBufferColor = reg.getTexture(ForwardRenderNode::name(), "baseColor");
    const Texture* gBufferNormal = reg.getTexture(ForwardRenderNode::name(), "normal");
    const Texture* gBufferDepth = reg.getTexture(ForwardRenderNode::name(), "depth");
    ASSERT(gBufferColor && gBufferNormal && gBufferDepth);

    constexpr size_t numSphereSamples = 23 * 256;
    constexpr size_t totalSphereSamplesSize = numSphereSamples * sizeof(vec4); // TODO: There are still problems with using GpuOptimal.. Not sure why.
    Buffer& sphereSampleBuffer = reg.createBuffer(totalSphereSamplesSize, Buffer::Usage::StorageBuffer, Buffer::MemoryHint::TransferOptimal);

    auto createStateForTLAS = [&](const TopLevelAS& tlas) -> std::pair<BindingSet&, RayTracingState&> {
        BindingSet& frameBindingSet = reg.createBindingSet({ { 0, ShaderStage(ShaderStageRTRayGen | ShaderStageRTClosestHit), &tlas },
                                                             { 1, ShaderStageRTRayGen, m_accumulationTexture, ShaderBindingType::StorageImage },
                                                             { 2, ShaderStageRTRayGen, gBufferColor, ShaderBindingType::TextureSampler },
                                                             { 3, ShaderStageRTRayGen, gBufferNormal, ShaderBindingType::TextureSampler },
                                                             { 4, ShaderStageRTRayGen, gBufferDepth, ShaderBindingType::TextureSampler },
                                                             { 5, ShaderStageRTRayGen, reg.getBuffer(SceneUniformNode::name(), "camera") },
                                                             { 6, ShaderStageRTMiss, reg.getBuffer(SceneUniformNode::name(), "environmentData") },
                                                             { 7, ShaderStageRTMiss, reg.getTexture(SceneUniformNode::name(), "environmentMap") },
                                                             { 8, ShaderStageRTClosestHit, reg.getBuffer(SceneUniformNode::name(), "directionalLight") },
                                                             { 9, ShaderStageRTRayGen, &sphereSampleBuffer, ShaderBindingType::StorageBuffer } });

        ShaderFile raygen = ShaderFile("rt-diffuseGI/raygen.rgen");
        HitGroup mainHitGroup { ShaderFile("rt-diffuseGI/closestHit.rchit") };
        HitGroup sphereSetHitGroup { ShaderFile("rt-diffuseGI/sphere.rchit"), {}, ShaderFile("rt-diffuseGI/sphere.rint") };
        std::vector<ShaderFile> missShaders { ShaderFile("rt-diffuseGI/miss.rmiss"),
                                              ShaderFile("rt-diffuseGI/shadow.rmiss") };
        ShaderBindingTable sbt { raygen, { mainHitGroup, sphereSetHitGroup }, missShaders };

        uint32_t maxRecursionDepth = 2;
        RayTracingState& rtState = reg.createRayTracingState(sbt, { &frameBindingSet, m_objectDataBindingSet }, maxRecursionDepth);

        return { frameBindingSet, rtState };
    };

    const TopLevelAS& mainTLAS = *reg.getTopLevelAccelerationStructure(RTAccelerationStructures::name(), "scene");
    auto& [frameBindingSet, rtState] = createStateForTLAS(mainTLAS);

    const TopLevelAS& proxyTLAS = *reg.getTopLevelAccelerationStructure(RTAccelerationStructures::name(), "proxy");
    auto& [frameBindingSetProxy, rtStateProxy] = createStateForTLAS(proxyTLAS);

    Texture& diffuseGI = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F, Texture::Usage::StorageAndSample);
    reg.publish("diffuseGI", diffuseGI);

    BindingSet& avgAccumBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, m_accumulationTexture, ShaderBindingType::StorageImage },
                                                            { 1, ShaderStageCompute, &diffuseGI, ShaderBindingType::StorageImage } });
    ComputeState& compAvgAccumState = reg.createComputeState(Shader::createCompute("rt-diffuseGI/averageAccum.comp"), { &avgAccumBindingSet });

    return [&](const AppState& appState, CommandList& cmdList) {
        static bool ignoreColor = false;
        static bool useProxies = false;
        if (ImGui::CollapsingHeader("Diffuse GI")) {
            ImGui::Checkbox("Ignore color", &ignoreColor);
            ImGui::Checkbox("Use proxies", &useProxies);
        }

        std::vector<float> sphereSamples;
        sphereSamples.resize(4 * numSphereSamples);
        for (size_t i = 0; i < numSphereSamples; ++i) {

            float x, y, z;
            float lengthSquared;

            do {
                x = m_bilateral(m_randomGenerator);
                y = m_bilateral(m_randomGenerator);
                z = m_bilateral(m_randomGenerator);
                lengthSquared = x * x + y * y + z * z;
            } while (lengthSquared > 1.0f);

            float length = std::sqrt(lengthSquared);
            sphereSamples[4 * i + 0] = x / length;
            sphereSamples[4 * i + 1] = y / length;
            sphereSamples[4 * i + 2] = z / length;
        }
        // TODO: It's quite slow to transfer all this data every frame. Probably better to instead upload a bunch initially
        //  and then just index into it & update every once in a while. Or something like that..
        cmdList.updateBufferImmediately(sphereSampleBuffer, sphereSamples.data(), totalSphereSamplesSize);

        if (useProxies) {
            cmdList.setRayTracingState(rtStateProxy);
            cmdList.bindSet(frameBindingSetProxy, 0);
        } else {
            cmdList.setRayTracingState(rtState);
            cmdList.bindSet(frameBindingSet, 0);
        }

        cmdList.bindSet(*m_objectDataBindingSet, 1);
        cmdList.pushConstant(ShaderStageRTRayGen, ignoreColor);

        cmdList.waitEvent(0, appState.frameIndex() == 0 ? PipelineStage::Host : PipelineStage::RayTracing);
        cmdList.resetEvent(0, PipelineStage::RayTracing);
        {
            if (m_scene.camera().didModify() || Input::instance().isKeyDown(GLFW_KEY_R)) {
                cmdList.clearTexture(*m_accumulationTexture, ClearColor(0, 0, 0));
                m_numAccumulatedFrames = 0;
            }

            cmdList.traceRays(appState.windowExtent());
            m_numAccumulatedFrames += 1;

            cmdList.debugBarrier(); // TODO: Add fine grained barrier here to make sure ray tracing is done before averaging!

            cmdList.setComputeState(compAvgAccumState);
            cmdList.bindSet(avgAccumBindingSet, 0);
            cmdList.pushConstant(ShaderStageCompute, m_numAccumulatedFrames);

            Extent2D totalSize = appState.windowExtent();
            constexpr uint32_t localSize = 16;
            cmdList.dispatch(totalSize.width() / localSize, totalSize.height() / localSize);
        }
        cmdList.signalEvent(0, PipelineStage::RayTracing);
    };
}
