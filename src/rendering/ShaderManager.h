#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

class ShaderManager {
public:
    enum class ShaderStatus {
        Good,
        FileNotFound,
        CompileError,
    };

    static ShaderManager& instance();

    ShaderManager() = delete;
    ShaderManager(ShaderManager&) = delete;
    ShaderManager(ShaderManager&&) = delete;

    void startFileWatching(unsigned msBetweenPolls);
    void stopFileWatching();

    [[nodiscard]] std::string resolvePath(const std::string& name) const;
    [[nodiscard]] std::optional<std::string> shaderError(const std::string& name) const;
    [[nodiscard]] std::optional<uint32_t> shaderVersion(const std::string& name) const;

    ShaderStatus loadAndCompileImmediately(const std::string& name);

    const std::vector<uint32_t>& spirv(const std::string& name) const;

private:
    explicit ShaderManager(std::string basePath);
    ~ShaderManager() = default;

    uint64_t getFileEditTimestamp(const std::string&) const;

    struct ShaderData {
        ShaderData() = default;
        ShaderData(std::string name, std::string path)
            : name(std::move(name))
            , path(std::move(path))
        {
        }

        std::string name {};
        std::string path {};

        uint64_t lastEditTimestamp { 0 };
        uint32_t currentBinaryVersion { 0 };
        bool lastEditSuccessfullyCompiled { false };
        std::string lastCompileError {};

        std::string glslSource {};
        std::vector<uint32_t> spirvBinary {};
    };

    bool compileGlslToSpirv(ShaderData& data) const;

    std::string m_shaderBasePath;
    std::unordered_map<std::string, ShaderData> m_loadedShaders {};

    std::unique_ptr<std::thread> m_fileWatcherThread {};
    mutable std::mutex m_shaderDataMutex {};
    volatile bool m_fileWatchingActive { false };
};
