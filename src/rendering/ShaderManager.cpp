#include "ShaderManager.h"

#include "utility/fileio.h"
#include "utility/logging.h"
#include "utility/util.h"
#include <chrono>
#include <cstddef>
#include <shaderc/shaderc.hpp>
#include <thread>

// TODO: Implement Windows support!
#include <sys/stat.h>

ShaderManager& ShaderManager::instance()
{
    static ShaderManager s_instance { "shaders" };
    return s_instance;
}

ShaderManager::ShaderManager(std::string basePath)
    : m_shaderBasePath(std::move(basePath))
{
}

void ShaderManager::startFileWatching(unsigned msBetweenPolls)
{
    if (m_fileWatcherThread != nullptr || m_fileWatchingActive) {
        return;
    }

    m_fileWatchingActive = true;
    m_fileWatcherThread = std::make_unique<std::thread>([this, msBetweenPolls]() {
        while (m_fileWatchingActive) {

            std::this_thread::sleep_for(std::chrono::milliseconds(msBetweenPolls));
            {
                //LogInfo("ShaderManager: update!\n");
                std::lock_guard<std::mutex> dataLock(m_shaderDataMutex);

                std::vector<std::string> filesToRemove {};
                for (auto& [_, data] : m_loadedShaders) {

                    if (!fileio::isFileReadable(data.path)) {
                        LogWarning("ShaderManager: removing shader '%s' from managed set since it seems to have been removed.\n");
                        filesToRemove.push_back(data.path);
                        continue;
                    }

                    uint64_t lastEdit = getFileEditTimestamp(data.path);
                    if (lastEdit > data.lastEditTimestamp) {
                        //LogInfo("Updating file '%s'\n", data.path.c_str());
                        data.glslSource = fileio::readEntireFile(data.path).value();
                        data.lastEditTimestamp = lastEdit;
                        if (compileGlslToSpirv(data)) {
                            data.currentBinaryVersion += 1;
                        } else {
                            LogError("Shader at path '%s' could not compile:\n\t%s\n", data.path.c_str(), data.lastCompileError.c_str());
                        }
                    }
                }

                for (const auto& path : filesToRemove) {
                    m_loadedShaders.erase(path);
                }
            }
        }
    });
}

void ShaderManager::stopFileWatching()
{
    m_fileWatchingActive = false;
    m_fileWatcherThread->join();
}

std::string ShaderManager::resolvePath(const std::string& name) const
{
    std::string resolvedPath = m_shaderBasePath + "/" + name;
    return resolvedPath;
}

std::optional<std::string> ShaderManager::shaderError(const std::string& name) const
{
    auto path = resolvePath(name);

    std::lock_guard<std::mutex> dataLock(m_shaderDataMutex);
    auto result = m_loadedShaders.find(path);

    if (result == m_loadedShaders.end()) {
        return {};
    }

    const ShaderData& data = result->second;
    if (data.lastEditSuccessfullyCompiled) {
        return {};
    }

    return data.lastCompileError;
}

std::optional<uint32_t> ShaderManager::shaderVersion(const std::string& name) const
{
    auto path = resolvePath(name);

    std::lock_guard<std::mutex> dataLock(m_shaderDataMutex);
    auto result = m_loadedShaders.find(path);

    if (result == m_loadedShaders.end()) {
        return {};
    }

    const ShaderData& data = result->second;
    return data.currentBinaryVersion;
}

ShaderManager::ShaderStatus ShaderManager::loadAndCompileImmediately(const std::string& name)
{
    auto path = resolvePath(name);

    std::lock_guard<std::mutex> dataLock(m_shaderDataMutex);

    auto result = m_loadedShaders.find(path);
    if (result == m_loadedShaders.end()) {

        if (!fileio::isFileReadable(path)) {
            return ShaderStatus::FileNotFound;
        }

        ShaderData data { name, path };
        data.glslSource = fileio::readEntireFile(path).value();
        data.lastEditTimestamp = getFileEditTimestamp(path);

        compileGlslToSpirv(data);

        m_loadedShaders[path] = std::move(data);
    }

    ShaderData& data = m_loadedShaders[path];
    if (data.lastEditSuccessfullyCompiled) {
        data.currentBinaryVersion = 1;
    } else {
        return ShaderStatus::CompileError;
    }

    return ShaderStatus::Good;
}

const std::vector<uint32_t>& ShaderManager::spirv(const std::string& name) const
{
    auto path = resolvePath(name);

    std::lock_guard<std::mutex> dataLock(m_shaderDataMutex);
    auto result = m_loadedShaders.find(path);

    // NOTE: This function should only be called from some backend, so if the
    //  file doesn't exist in the set of loaded shaders something is wrong,
    //  because the frontend makes sure to not run if shaders don't work.
    ASSERT(result != m_loadedShaders.end());

    const ShaderData& data = result->second;
    return data.spirvBinary;
}

uint64_t ShaderManager::getFileEditTimestamp(const std::string& path) const
{
    struct stat statResult = {};
    if (stat(path.c_str(), &statResult) == 0) {
        return statResult.st_mtime;
    }

    ASSERT_NOT_REACHED();
}

bool ShaderManager::compileGlslToSpirv(ShaderData& data) const
{
    ASSERT(!data.glslSource.empty());

    shaderc::Compiler compiler {};
    shaderc::CompileOptions options {};

    class Includer : public shaderc::CompileOptions::IncluderInterface {
    public:
        explicit Includer(const ShaderManager& shaderManager)
            : m_shaderManager(shaderManager)
        {
        }

        struct FileData {
            std::string path;
            std::string content;
        };

        shaderc_include_result* GetInclude(const char* requested_source, shaderc_include_type type, const char* requesting_source, size_t include_depth) override
        {
            auto* data = new shaderc_include_result();

            auto* fileData = new FileData();
            fileData->path = m_shaderManager.resolvePath(requested_source);
            fileData->content = fileio::readEntireFile(fileData->path).value();
            data->user_data = fileData;

            data->source_name = fileData->path.c_str();
            data->source_name_length = fileData->path.size();

            data->content = fileData->content.c_str();
            data->content_length = fileData->content.size();

            LogInfo("Getting include '%s' from '%s' with depth %d!\n", requested_source, requesting_source, include_depth);
            return data;
        }

        void ReleaseInclude(shaderc_include_result* data) override
        {
            delete (FileData*)data->user_data;
            delete data;
        }

    private:
        const ShaderManager& m_shaderManager;
    };
    options.SetIncluder(std::make_unique<Includer>(*this));

    // TODO: Why doesn't the automatic inferring work?!
    shaderc_shader_kind kind = shaderc_glsl_infer_from_source;
    bool isProbablyVert = data.name.find(".vert") != std::string::npos;
    bool isProbablyFrag = data.name.find(".frag") != std::string::npos;
    if (isProbablyVert && !isProbablyFrag)
        kind = shaderc_glsl_vertex_shader;
    else if (isProbablyFrag && !isProbablyVert)
        kind = shaderc_glsl_fragment_shader;
    else
        ASSERT_NOT_REACHED();

    shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(data.glslSource, kind, data.name.c_str(), options);

    // Note that we only should overwrite the binary if it compiled correctly!
    if (module.GetCompilationStatus() != shaderc_compilation_status_success) {
        data.lastEditSuccessfullyCompiled = false;
        data.lastCompileError = module.GetErrorMessage();
    } else {
        data.lastEditSuccessfullyCompiled = true;
        data.lastCompileError.clear();
        data.spirvBinary = std::vector<uint32_t>(module.cbegin(), module.cend());
    }

    return data.lastEditSuccessfullyCompiled;
}
