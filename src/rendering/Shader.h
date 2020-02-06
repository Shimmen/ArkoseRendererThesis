#pragma once

#include <string>
#include <vector>

enum class ShaderFileType {
    Vertex,
    Fragment,
    Compute,
};

struct ShaderFile {
    ShaderFile(std::string name, ShaderFileType);

    [[nodiscard]] const std::string& name() const;
    [[nodiscard]] ShaderFileType type() const;

private:
    std::string m_name;
    ShaderFileType m_type;
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
