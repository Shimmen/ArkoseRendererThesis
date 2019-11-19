#include "ShaderManager.h"

#include "utility/GlobalState.h"
#include "utility/fileio.h"
#include "utility/logging.h"
#include "utility/util.h"
#include <chrono>
#include <filesystem>
#include <thread>

ShaderManager& ShaderManager::instance()
{
    static ShaderManager s_instance { "shaders" };

    if (!s_instance.fileWatcherActive()) {
        s_instance.startFileWatching(500);
    }

    return s_instance;
}

ShaderManager::ShaderManager(std::string basePath)
    : m_shaderBasePath(std::move(basePath))
{
}

std::string ShaderManager::resolvePath(const std::string& name) const
{
    std::filesystem::path resolvedPath = std::filesystem::current_path() / m_shaderBasePath / name;
    return resolvedPath.string();
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
        data.lastEditTimestamp = std::filesystem::last_write_time(path).time_since_epoch().count();

        m_loadedShaders[path] = std::move(data);
    }

    ShaderData& data = m_loadedShaders[path];
    bool compilationSuccess = compileGlslToSpirv(data);

    if (!compilationSuccess) {
        return ShaderStatus::CompileError;
    }

    return ShaderStatus::Good;
}

ShaderManager::SpirV ShaderManager::spirv(const std::string& name) const
{
    auto path = resolvePath(name);

    std::lock_guard<std::mutex> dataLock(m_shaderDataMutex);
    auto result = m_loadedShaders.find(path);

    // NOTE: This function should only be called from some backend, so if the
    //  file doesn't exist in the set of loaded shaders something is wrong,
    //  because the frontend makes sure to not run if shaders don't work.
    ASSERT(result != m_loadedShaders.end());

    const ShaderData& data = result->second;
    auto* code = reinterpret_cast<const uint32_t*>(data.spirvBinary.c_str()); // TODO: Do we need to consider memory alignment for uint32_t?
    return { .code = code, .size = data.spirvBinary.size() };
}

bool ShaderManager::fileWatcherActive() const
{
    return m_fileWatcherThread != nullptr;
}

void ShaderManager::startFileWatching(unsigned msBetweenPolls)
{
    if (fileWatcherActive()) {
        return;
    }

    m_fileWatcherThread = std::make_unique<std::thread>([&]() {
        while (GlobalState::get().applicationRunning()) {

            std::this_thread::sleep_for(std::chrono::milliseconds(msBetweenPolls));
            {
                std::lock_guard<std::mutex> dataLock(m_shaderDataMutex);

                std::vector<std::string> filesToRemove {};
                for (auto& [_, data] : m_loadedShaders) {

                    if (!fileio::isFileReadable(data.path)) {
                        LogWarning("ShaderManager: removing shader '%s' from managed set since it seems to have been removed.\n");
                        filesToRemove.push_back(data.path);
                        continue;
                    }

                    uint64_t lastWrite = std::filesystem::last_write_time(data.path).time_since_epoch().count();
                    if (lastWrite > data.lastEditTimestamp) {
                        data.glslSource = fileio::readEntireFile(data.path).value();
                        data.lastEditTimestamp = lastWrite;
                        if (!compileGlslToSpirv(data)) {
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

bool ShaderManager::compileGlslToSpirv(ShaderData& data) const
{
    ASSERT(!data.glslSource.empty());

    // TODO: Actually compile GLSL to SPIR-V!

    // TODO: Note that we only should overwrite the binary if it compiled correctly!
    data.spirvBinary = "";
    data.lastEditSuccessfullyCompiled = false;
    data.lastCompileError = "Compiling not yet implemented!!\n";

    return data.lastEditSuccessfullyCompiled;
}
