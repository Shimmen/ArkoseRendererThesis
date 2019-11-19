#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

class ShaderManager {
public:
    enum class ShaderStatus {
        Good,
        FileNotFound,
        CompileError,
    };

    struct SpirV {
        const uint32_t* code;
        const size_t size;
    };

    static ShaderManager& instance();

    ShaderManager() = delete;
    ShaderManager(ShaderManager&) = delete;
    ShaderManager(ShaderManager&&) = delete;

    [[nodiscard]] std::string resolvePath(const std::string& name) const;
    [[nodiscard]] std::optional<std::string> shaderError(const std::string& name) const;

    ShaderStatus loadAndCompileImmediately(const std::string& name);

    //! The return value should be used immediately since there are no lifetime guarantees beyond the calling scope
    SpirV spirv(const std::string& name) const;

private:
    explicit ShaderManager(std::string basePath);
    ~ShaderManager() = default;

    bool fileWatcherActive() const;
    void startFileWatching(unsigned msBetweenPolls);

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
        bool lastEditSuccessfullyCompiled { false };
        std::string lastCompileError {};

        std::string glslSource {};
        std::string spirvBinary {};
    };

    bool compileGlslToSpirv(ShaderData& data) const;

    std::string m_shaderBasePath;
    std::unordered_map<std::string, ShaderData> m_loadedShaders {};

    std::unique_ptr<std::thread> m_fileWatcherThread {};
    mutable std::mutex m_shaderDataMutex {};
};
