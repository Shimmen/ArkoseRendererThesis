#pragma once

#include <string>
#include <vector>

enum class ShaderStage : uint8_t {
    Vertex = 0x01,
    Fragment = 0x02,
    Compute = 0x04,
};

inline ShaderStage operator|(ShaderStage lhs, ShaderStage rhs)
{
    auto left = static_cast<uint8_t>(lhs);
    auto right = static_cast<uint8_t>(rhs);
    return static_cast<ShaderStage>(left | right);
}

struct ShaderFile {
    ShaderFile(std::string name, ShaderStage);

    [[nodiscard]] const std::string& name() const;
    [[nodiscard]] ShaderStage stage() const;

private:
    std::string m_name;
    ShaderStage m_stage;
};

enum class ShaderType {
    Raster,
    Compute
};

struct Shader {

    static Shader createVertexOnly(std::string name, std::string vertexName);
    static Shader createBasic(std::string name, std::string vertexName, std::string fragmentName);
    static Shader createCompute(std::string name, std::string computeName);

    Shader() = default;
    Shader(std::string name, std::vector<ShaderFile>, ShaderType type);
    ~Shader();

    [[nodiscard]] ShaderType type() const;
    [[nodiscard]] const std::vector<ShaderFile>& files() const;

    // TODO: We should maybe add some utility API for shader introspection here..?
    //  Somehow we need to extract descriptor sets etc.
    //  but maybe that is backend-specific or file specific?

private:
    std::string m_name {};
    std::vector<ShaderFile> m_files {};
    ShaderType m_type {};
};
